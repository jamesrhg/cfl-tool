#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cfl_mii.h"
#include "dbglog.h"

#define CLEAR_COLOR 0x404040FF

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

typedef struct { float position[3]; float normal[3]; float texcoord[2]; } Vertex;

static C3D_Mtx projection;

static C3D_LightEnv sceneLightEnv;
static C3D_Light sceneLight;
static C3D_LightLut sceneSpecularLut;

static float scale = 0.032f;
static float yaw = 0.0f;
static float pitch = 0.0f;
static float headX = 0.0f;
static float headY = 0.0f;
static float cameraDist = 3.2f;

#define MAX_TEST_MODELS 6
static CFLCharModel models[MAX_TEST_MODELS];
static int modelCount = 0;
static CFLExpression currentExpression = CFL_EXPRESSION_NORMAL;
static CFLExpressionFlag testExpressionFlags = CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL);
static float modelSpin[MAX_TEST_MODELS];

typedef enum {
	SCREEN_TITLE,
	SCREEN_CHARMODEL_SUBMENU,
	SCREEN_CHARMODEL_TEST,
	SCREEN_ICON_TEST,
} AppScreen;
static AppScreen screen = SCREEN_TITLE;

#define TITLE_OPTION_COUNT 2
static const char* kTitleOptions[TITLE_OPTION_COUNT] = {
	"CharModel only Test",
	"Icon Test",
};
static int titleSelection = 0;

typedef struct {
	const char* label;
	int modelCount;
	CFLResolution resolution;
	CFLExpressionFlag expressionFlags;
} CharModelTestConfig;

static const CharModelTestConfig kCharModelConfigs[] = {
	{ "1 CharModel, 512 Tex, Normal",
	  1, CFL_RESOLUTION_512, CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL) },
	{ "1 CharModel, 256 Tex, All Exp",
	  1, CFL_RESOLUTION_256, CFL_EXPRESSION_FLAG_ALL },
	{ "2 CharModel, 256 Tex, Normal + Blink",
	  2, CFL_RESOLUTION_256, CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL) | CFL_EXPRESSION_FLAG(CFL_EXPRESSION_BLINK) },
	{ "4 CharModel, 128 Tex, Normal + Smile",
	  4, CFL_RESOLUTION_128, CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL) | CFL_EXPRESSION_FLAG(CFL_EXPRESSION_SMILE) },
	{ "6 CharModel, 64 Tex, Normal",
	  6, CFL_RESOLUTION_64, CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL) },
};
#define CHARMODEL_CONFIG_COUNT (int)(sizeof(kCharModelConfigs) / sizeof(kCharModelConfigs[0]))
static int submenuSelection = 0;

static CFLCharModel iconModel;
static C3D_Tex iconTexture256;
static C3D_Tex iconTexture128;
static bool iconTexture256Valid = false;
static bool iconTexture128Valid = false;

static u32 s_lastMiiSelectorIndex = 0;

static bool selectMii(MiiData* mii)
{
	memset(mii, 0, sizeof(*mii));

	MiiSelectorConf conf;
	MiiSelectorReturn ret;
	miiSelectorInit(&conf);
	miiSelectorSetTitle(&conf, "Select a Mii for the demo");
	miiSelectorSetOptions(&conf, MIISELECTOR_CANCEL);
	miiSelectorSetInitialIndex(&conf, s_lastMiiSelectorIndex);
	miiSelectorLaunch(&conf, &ret);

	if (ret.guest_mii_was_selected && ret.guest_mii_index != 0xFFFFFFFF)
		s_lastMiiSelectorIndex = ret.guest_mii_index;

	if (!ret.no_mii_selected && miiSelectorChecksumIsValid(&ret)) {
		*mii = ret.mii;
		char name[36];
		miiSelectorReturnGetName(&ret, name, sizeof(name));
		dbglog("Selected Mii: %s\n", name);
		return true;
	}
	dbglog("No Mii selected, using defaults.\n");
	return false;
}

static CFLExpression nextTestExpression(CFLExpression from)
{
	for (int step = 1; step <= CFL_EXPRESSION_COUNT; step++) {
		CFLExpression candidate = (CFLExpression)(((int)from + step) % CFL_EXPRESSION_COUNT);
		if (testExpressionFlags & CFL_EXPRESSION_FLAG(candidate)) return candidate;
	}
	return from;
}

static void setMaterialColor(const float color[3], bool noSpecular)
{
	static const float kShadowColor[3] = { 0.10196f, 0.09020f, 0.07843f };

	C3D_Material mtl;
	for (int i = 0; i < 3; i++) {
		float c = color[2 - i];
		mtl.ambient[i] = c * (1.0f - kShadowColor[i]);
		mtl.diffuse[i] = c * kShadowColor[i];
	}
	float spec = noSpecular ? 0.0f : 0.42f;
	mtl.specular0[0] = mtl.specular0[1] = mtl.specular0[2] = spec;
	mtl.specular1[0] = mtl.specular1[1] = mtl.specular1[2] = 0.0f;
	mtl.emission[0] = mtl.emission[1] = mtl.emission[2] = 0.0f;
	C3D_LightEnvMaterial(&sceneLightEnv, &mtl);
}

static void getSlotOffset(int index, int count, float* outX, float* outY)
{
	static const float SPACING = 3.3f;
	if (count <= 1) { *outX = 0.0f; *outY = 0.0f; return; }
	*outX = ((float)index - (float)(count - 1) * 0.5f) * SPACING;
	*outY = 0.0f;
}

static void sceneRenderModel(const CFLCharModel* cm, float slotX, float slotY, float spinYaw)
{
	CFLShaderLocations loc = CFL_GetShaderLocations();

	C3D_Mtx modelView;
	Mtx_Identity(&modelView);
	Mtx_Translate(&modelView, headX + slotX, headY + slotY, -cameraDist, true);
	Mtx_RotateX(&modelView, pitch, true);
	Mtx_RotateY(&modelView, yaw, true);
	Mtx_RotateY(&modelView, spinYaw, true);
	Mtx_Scale(&modelView, scale, scale, scale);

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.projection, &projection);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.modelView,  &modelView);

	C3D_LightEnvBind(&sceneLightEnv);

	{
		C3D_TexEnv* env1 = C3D_GetTexEnv(1);
		C3D_TexEnvInit(env1);
		C3D_TexEnvSrc(env1, C3D_RGB, GPU_PREVIOUS, GPU_FRAGMENT_SECONDARY_COLOR, 0);
		C3D_TexEnvFunc(env1, C3D_RGB, GPU_ADD);
		C3D_TexEnvSrc(env1, C3D_Alpha, GPU_PREVIOUS, 0, 0);
		C3D_TexEnvFunc(env1, C3D_Alpha, GPU_REPLACE);
		C3D_DirtyTexEnv(env1);
		C3D_TexEnv* env2 = C3D_GetTexEnv(2);
		C3D_TexEnvInit(env2);
		C3D_DirtyTexEnv(env2);
	}

	int partCount = CFL_GetPartCount(cm);
	for (int pass = 0; pass < 2; pass++) {
		bool texturedPass = (pass == 1);
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);

		for (int i = 0; i < partCount; i++) {
			const CFLPart* part = CFL_GetPart(cm, i);
			if (part->hasTexture != texturedPass) continue;

			C3D_DepthTest(true, GPU_GEQUAL, part->depthWrite ? GPU_WRITE_ALL : GPU_WRITE_COLOR);

			C3D_BufInfo* bufInfo = C3D_GetBufInfo();
			BufInfo_Init(bufInfo);
			BufInfo_Add(bufInfo, part->vbo, sizeof(Vertex), 3, 0x210);

			setMaterialColor(part->color, part->noSpecular);

			C3D_TexEnv* env = C3D_GetTexEnv(0);
			if (part->hasTexture) {
				C3D_TexBind(0, (C3D_Tex*)&part->tex);
				C3D_TexEnvInit(env);
				if (part->isAlphaOnly) {
					C3D_TexEnvSrc(env, C3D_RGB, GPU_FRAGMENT_PRIMARY_COLOR, 0, 0);
					C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
				} else {
					C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_FRAGMENT_PRIMARY_COLOR, 0);
					C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
				}
				C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
				C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
			} else {
				C3D_TexEnvInit(env);
				C3D_TexEnvSrc(env, C3D_Both, GPU_FRAGMENT_PRIMARY_COLOR, 0, 0);
				C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
			}

			if (part->useIndices)
				C3D_DrawElements(GPU_TRIANGLES, part->indexCount, C3D_UNSIGNED_BYTE, part->ibo);
			else
				C3D_DrawArrays(GPU_TRIANGLES, 0, part->vertexCount);
		}
	}
}

static void sceneRender(void)
{
	CFL_RebindShader();
	for (int i = 0; i < modelCount; i++) {
		if (!CFL_HasCharModel(&models[i])) continue;
		float slotX, slotY;
		getSlotOffset(i, modelCount, &slotX, &slotY);
		sceneRenderModel(&models[i], slotX, slotY, modelSpin[i]);
	}
}

static C2D_TextBuf s_textBuf;

static void drawExpressionOverlay(C3D_RenderTarget* target)
{
	char label[64];
	snprintf(label, sizeof(label), "Expression: %s   (SELECT: replace last Mii)",
		CFL_GetExpressionName(currentExpression));

	C2D_Prepare();
	C2D_SceneBegin(target);
	C2D_TextBufClear(s_textBuf);
	C2D_Text text;
	C2D_TextParse(&text, s_textBuf, label);
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor, 10.0f, 10.0f, 0.0f, 0.7f, 0.7f, C2D_Color32(255, 255, 255, 255));
}

static void drawTextMenu(C3D_RenderTarget* target, const char* heading, const char* const* options, int count, int selection, const char* footer)
{
	C2D_Prepare();
	C2D_SceneBegin(target);
	C2D_TextBufClear(s_textBuf);

	C2D_Text headingText;
	C2D_TextParse(&headingText, s_textBuf, heading);
	C2D_TextOptimize(&headingText);
	C2D_DrawText(&headingText, C2D_WithColor, 20.0f, 20.0f, 0.0f, 1.0f, 1.0f, C2D_Color32(255, 255, 255, 255));

	for (int i = 0; i < count; i++) {
		char line[96];
		snprintf(line, sizeof(line), "%s %s", (i == selection) ? ">" : " ", options[i]);
		C2D_Text optionText;
		C2D_TextParse(&optionText, s_textBuf, line);
		C2D_TextOptimize(&optionText);
		u32 color = (i == selection) ? C2D_Color32(255, 255, 0, 255) : C2D_Color32(220, 220, 220, 255);
		C2D_DrawText(&optionText, C2D_WithColor, 30.0f, 60.0f + i * 24.0f, 0.0f, 0.75f, 0.75f, color);
	}

	if (footer) {
		C2D_Text footerText;
		C2D_TextParse(&footerText, s_textBuf, footer);
		C2D_TextOptimize(&footerText);
		C2D_DrawText(&footerText, C2D_WithColor, 20.0f, 210.0f, 0.0f, 0.6f, 0.6f, C2D_Color32(180, 180, 180, 255));
	}
}

static void destroyTestModels(void)
{
	for (int i = 0; i < modelCount; i++) CFL_DestroyCharModel(&models[i]);
	modelCount = 0;
}

static CFLResolution activeResolution = CFL_RESOLUTION_256;
static CFLExpressionFlag activeExpressionFlags = CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL);

static void startCharModelTest(const CharModelTestConfig* cfg)
{
	destroyTestModels();
	testExpressionFlags = cfg->expressionFlags;
	activeResolution = cfg->resolution;
	activeExpressionFlags = cfg->expressionFlags;
	currentExpression = CFL_EXPRESSION_NORMAL;

	for (int i = 0; i < cfg->modelCount && i < MAX_TEST_MODELS; i++) {
		MiiData mii;
		selectMii(&mii);
		dbglog_vram_stats("startCharModelTest before CFL_InitCharModel", false);
		modelCount = i + 1;
		if (!CFL_InitCharModel(&models[i], &mii, cfg->resolution, cfg->expressionFlags)) {
			dbglog_err("\nCould not build CharModel %d/%d for this test.\n", i + 1, cfg->modelCount);
			dbglog_vram_stats("startCharModelTest CFL_InitCharModel failure", true);
			continue;
		}
		CFL_SetExpression(&models[i], currentExpression);
		modelSpin[i] = 0.0f;
	}

	cameraDist = 3.2f + (float)(cfg->modelCount - 1) * 2.2f;
	yaw = 0.0f;
	pitch = 0.0f;
	headX = 0.0f;
	headY = 0.0f;
	screen = SCREEN_CHARMODEL_TEST;
}

static void startIconTest(void)
{
	CFL_DestroyCharModel(&iconModel);
	if (iconTexture256Valid) { C3D_TexDelete(&iconTexture256); iconTexture256Valid = false; }
	if (iconTexture128Valid) { C3D_TexDelete(&iconTexture128); iconTexture128Valid = false; }

	MiiData mii;
	selectMii(&mii);
	if (!CFL_InitCharModel(&iconModel, &mii, CFL_RESOLUTION_256, CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL))) {
		dbglog_err("\nIcon Test: could not build a CharModel from this Mii.\n");
		screen = SCREEN_TITLE;
		return;
	}
	if (!CFL_CommandMakeModelIcon(&iconModel, CFL_EXPRESSION_NORMAL, 256, NULL, &iconTexture256)) {
		dbglog_err("\nIcon Test: CFL_CommandMakeModelIcon (256) failed.\n");
		CFL_DestroyCharModel(&iconModel);
		screen = SCREEN_TITLE;
		return;
	}
	iconTexture256Valid = true;
	if (!CFL_CommandMakeModelIcon(&iconModel, CFL_EXPRESSION_NORMAL, 128, NULL, &iconTexture128)) {
		dbglog_err("\nIcon Test: CFL_CommandMakeModelIcon (128) failed.\n");
		C3D_TexDelete(&iconTexture256);
		iconTexture256Valid = false;
		CFL_DestroyCharModel(&iconModel);
		screen = SCREEN_TITLE;
		return;
	}
	iconTexture128Valid = true;
	screen = SCREEN_ICON_TEST;
}

#define ICON_QUAD_SLOTS 2
static Vertex* s_iconQuadVBO[ICON_QUAD_SLOTS];
static u8* s_iconQuadIBO[ICON_QUAD_SLOTS];

static void drawIconQuad(int slot, C3D_Tex* tex, float centerX, float centerY, float halfSize)
{
	if (!s_iconQuadVBO[slot]) {
		s_iconQuadVBO[slot] = (Vertex*)linearAlloc(sizeof(Vertex) * 4);
		s_iconQuadIBO[slot] = (u8*)linearAlloc(6);
		static const u8 kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };
		memcpy(s_iconQuadIBO[slot], kQuadIndices, sizeof(kQuadIndices));
	}
	Vertex* quad = s_iconQuadVBO[slot];
	quad[0] = (Vertex){ { centerX + halfSize, centerY - halfSize, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } };
	quad[1] = (Vertex){ { centerX + halfSize, centerY + halfSize, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } };
	quad[2] = (Vertex){ { centerX - halfSize, centerY + halfSize, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } };
	quad[3] = (Vertex){ { centerX - halfSize, centerY - halfSize, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } };

	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, quad, sizeof(Vertex), 3, 0x210);

	C3D_TexBind(0, tex);
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_FRAGMENT_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);

	C3D_DrawElements(GPU_TRIANGLES, 6, C3D_UNSIGNED_BYTE, s_iconQuadIBO[slot]);
}

static void drawIconTest(C3D_RenderTarget* target)
{
	CFL_RebindShader();
	CFLShaderLocations loc = CFL_GetShaderLocations();

	C3D_Mtx modelView;
	Mtx_Identity(&modelView);
	Mtx_Translate(&modelView, 0.0f, 0.0f, -2.0f, true);

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.projection, &projection);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.modelView,  &modelView);
	C3D_LightEnvBind(&sceneLightEnv);

	{
		C3D_TexEnv* env1 = C3D_GetTexEnv(1);
		C3D_TexEnvInit(env1);
		C3D_DirtyTexEnv(env1);
		C3D_TexEnv* env2 = C3D_GetTexEnv(2);
		C3D_TexEnvInit(env2);
		C3D_DirtyTexEnv(env2);
	}

	static const float white[3] = { 1.0f, 1.0f, 1.0f };
	setMaterialColor(white, true);

	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);

	static const float kBottomY = -0.9f;
	float half256 = 0.7f, half128 = 0.35f;
	if (iconTexture256Valid)
		drawIconQuad(0, &iconTexture256, -0.75f, kBottomY + half256, half256);
	if (iconTexture128Valid)
		drawIconQuad(1, &iconTexture128, 0.65f, kBottomY + half128, half128);

	C2D_Prepare();
	C2D_SceneBegin(target);
	C2D_TextBufClear(s_textBuf);
	C2D_Text text;
	C2D_TextParse(&text, s_textBuf, "Icon Test - CFL_CommandMakeModelIcon() 256px + 128px   (B: back)");
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor, 10.0f, 10.0f, 0.0f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
}

static void waitForStartAndExit(void)
{
	dbglog("\nPress START to exit.\n");
	while (aptMainLoop()) {
		hidScanInput();
		if (hidKeysDown() & KEY_START) break;
		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}
}

int main(void)
{
	gfxInitDefault();
	consoleInit(GFX_BOTTOM, NULL);

	dbglog_open();
	dbglog("CFL Tool\n\n");
	dbglog("Log file: sdmc:/3ds/cfl_test.txt\n\n");

	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C3D_RenderTarget* target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
	if (!CFL_Initialize()) {
		dbglog_err("\nCFL_Initialize failed - see the log above for specifics\n");
		dbglog("(most likely the archive read needing full ARM11 FS\n");
		dbglog("permissions - launch via Luma3DS/Rosalina's homebrew\n");
		dbglog("launcher).\n");
		C3D_Fini();
		waitForStartAndExit();
		dbglog_close();
		gfxExit();
		return 0;
	}
	Mtx_PerspTilt(&projection, C3D_AngleFromDegrees(50.0f), C3D_AspectRatioTop, 0.01f, 1000.0f, false);

	C3D_LightEnvInit(&sceneLightEnv);
	C3D_LightEnvAmbient(&sceneLightEnv, 0.0f, 0.0f, 0.0f);
	LightLut_Phong(&sceneSpecularLut, 8.0f);
	C3D_LightEnvLut(&sceneLightEnv, GPU_LUT_D0, GPU_LUTINPUT_NH, false, &sceneSpecularLut);
	C3D_LightInit(&sceneLight, &sceneLightEnv);
	{
		C3D_FVec lightDir = FVec4_New(-0.53906f, 0.53906f, 0.64697f, 0.0f);
		C3D_LightPosition(&sceneLight, &lightDir);
	}
	C3D_LightAmbient(&sceneLight, 1.0f, 1.0f, 1.0f);
	C3D_LightDiffuse(&sceneLight, 1.0f, 1.0f, 1.0f);
	C3D_LightSpecular0(&sceneLight, 0.99608f, 0.99608f, 0.99608f);
	C3D_LightSpecular1(&sceneLight, 0.0f, 0.0f, 0.0f);
	C3D_LightEnable(&sceneLight, true);

	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	s_textBuf = C2D_TextBufNew(256);

	dbglog("\nCFL Tool ready.\n");
	dbglog("D-Pad: menu navigation / move head   A: confirm   B: back   START: exit\n");
	dbglog("In a CharModel test: Circle Pad look, L/R zoom, X change expression\n");

	while (aptMainLoop()) {
		hidScanInput();
		u32 kDown = hidKeysDown();
		if (kDown & KEY_START) break;

		switch (screen) {
		case SCREEN_TITLE:
			if (kDown & KEY_DUP)   titleSelection = (titleSelection + TITLE_OPTION_COUNT - 1) % TITLE_OPTION_COUNT;
			if (kDown & KEY_DDOWN) titleSelection = (titleSelection + 1) % TITLE_OPTION_COUNT;
			if (kDown & KEY_A) {
				if (titleSelection == 0) {
					submenuSelection = 0;
					screen = SCREEN_CHARMODEL_SUBMENU;
				} else {
					startIconTest();
				}
			}
			break;

		case SCREEN_CHARMODEL_SUBMENU:
			if (kDown & KEY_DUP)   submenuSelection = (submenuSelection + CHARMODEL_CONFIG_COUNT - 1) % CHARMODEL_CONFIG_COUNT;
			if (kDown & KEY_DDOWN) submenuSelection = (submenuSelection + 1) % CHARMODEL_CONFIG_COUNT;
			if (kDown & KEY_A) startCharModelTest(&kCharModelConfigs[submenuSelection]);
			if (kDown & KEY_B) screen = SCREEN_TITLE;
			break;

		case SCREEN_CHARMODEL_TEST:
			if (kDown & KEY_B) {
				destroyTestModels();
				screen = SCREEN_TITLE;
				break;
			}
			if ((kDown & KEY_SELECT) && modelCount > 0) {
				int slot = modelCount - 1;
				MiiData mii;
				if (selectMii(&mii)) {
					CFL_DestroyCharModel(&models[slot]);
					if (CFL_InitCharModel(&models[slot], &mii, activeResolution, activeExpressionFlags)) {
						CFL_SetExpression(&models[slot], currentExpression);
						modelSpin[slot] = 0.0f;
					} else {
						dbglog_err("\nCould not rebuild CharModel %d after reselect.\n", slot);
					}
				}
			}
			if (kDown & KEY_X) {
				currentExpression = nextTestExpression(currentExpression);
				for (int i = 0; i < modelCount; i++) {
					if (CFL_SetExpression(&models[i], currentExpression))
						dbglog("Mii %d expression: %s\n", i, CFL_GetExpressionName(currentExpression));
				}
			}

			if (hidKeysHeld() & KEY_L) cameraDist *= 1.02f;
			if (hidKeysHeld() & KEY_R) cameraDist *= 0.98f;
			if (cameraDist < 0.3f) cameraDist = 0.3f;
			if (cameraDist > 40.0f) cameraDist = 40.0f;

			if (hidKeysHeld() & KEY_DLEFT)  headX -= 0.08f;
			if (hidKeysHeld() & KEY_DRIGHT) headX += 0.08f;
			if (hidKeysHeld() & KEY_DUP)    headY += 0.08f;
			if (hidKeysHeld() & KEY_DDOWN)  headY -= 0.08f;

			{
				circlePosition cpos;
				hidCircleRead(&cpos);
				float cx = cpos.dx / 156.0f;
				float cy = cpos.dy / 156.0f;
				if (fabsf(cx) < 0.15f) cx = 0.0f;
				if (fabsf(cy) < 0.15f) cy = 0.0f;

				if (cx != 0.0f || cy != 0.0f) {
					yaw += cx * 0.05f;
					pitch += cy * 0.05f;
					if (pitch > 1.3f) pitch = 1.3f;
					if (pitch < -1.3f) pitch = -1.3f;
				}
			}

			for (int i = 0; i < modelCount; i++) modelSpin[i] += 0.015f;
			break;

		case SCREEN_ICON_TEST:
			if (kDown & KEY_B) {
				CFL_DestroyCharModel(&iconModel);
				if (iconTexture256Valid) { C3D_TexDelete(&iconTexture256); iconTexture256Valid = false; }
				if (iconTexture128Valid) { C3D_TexDelete(&iconTexture128); iconTexture128Valid = false; }
				screen = SCREEN_TITLE;
			}
			break;
		}

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
			C3D_RenderTargetClear(target, C3D_CLEAR_ALL, CLEAR_COLOR, 0);
			C3D_FrameDrawOn(target);
			switch (screen) {
			case SCREEN_TITLE:
				drawTextMenu(target, "CFL Tool", kTitleOptions, TITLE_OPTION_COUNT, titleSelection,
					"D-Pad: select   A: confirm   START: exit");
				break;
			case SCREEN_CHARMODEL_SUBMENU: {
				const char* labels[CHARMODEL_CONFIG_COUNT];
				for (int i = 0; i < CHARMODEL_CONFIG_COUNT; i++) labels[i] = kCharModelConfigs[i].label;
				drawTextMenu(target, "CharModel only Test", labels, CHARMODEL_CONFIG_COUNT, submenuSelection,
					"D-Pad: select   A: confirm   B: back");
				break;
			}
			case SCREEN_CHARMODEL_TEST:
				sceneRender();
				drawExpressionOverlay(target);
				break;
			case SCREEN_ICON_TEST:
				drawIconTest(target);
				break;
			}
		C3D_FrameEnd(0);
	}

	destroyTestModels();
	CFL_DestroyCharModel(&iconModel);
	if (iconTexture256Valid) C3D_TexDelete(&iconTexture256);
	if (iconTexture128Valid) C3D_TexDelete(&iconTexture128);
	for (int i = 0; i < ICON_QUAD_SLOTS; i++) {
		if (s_iconQuadVBO[i]) linearFree(s_iconQuadVBO[i]);
		if (s_iconQuadIBO[i]) linearFree(s_iconQuadIBO[i]);
	}
	CFL_Finalize();

	C2D_TextBufDelete(s_textBuf);
	C2D_Fini();
	C3D_Fini();
	dbglog_close();
	gfxExit();
	return 0;
}

