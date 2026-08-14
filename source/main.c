
#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cfl_mii.h"

#include "MaleBody_iqm.h"
#include "FemaleBody_iqm.h"

#include "StreetPassBodyMale_iqm.h"
#include "StreetPassBodyFemale_iqm.h"

#include "IconBody_iqm.h"

#include "DefaultAnim_iqm.h"

#define CLEAR_COLOR 0x404040FF

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

typedef struct { float position[3]; float normal[3]; float texcoord[2]; } Vertex;

#define BODY_MAX_PARTS 4

#define BODY_MAX_BONES 24

typedef struct {
	void* vbo;
	void* ibo;
	u32 vertexCount;
	u32 indexCount;
	float color[3];
} BodyPart;

typedef struct BodyModel {
	BodyPart parts[BODY_MAX_PARTS];
	int partCount;
	bool hasHeadBone;
	float headBoneWorldMatrix[12];
	float bodyScale[3];
	
	float headWorldScale;
	void* rig;
} BodyModel;

typedef struct {
	float translate[3];
	float rotate[4];
	float scale[3];
} BoneLocalPose;

static const float pantsColors[4][3] = {
	{ 0x40 / 255.0f, 0x47 / 255.0f, 0x4E / 255.0f },
	{ 0x28 / 255.0f, 0x40 / 255.0f, 0x7A / 255.0f },
	{ 0x70 / 255.0f, 0x20 / 255.0f, 0x15 / 255.0f },
	{ 0xC0 / 255.0f, 0xA0 / 255.0f, 0x30 / 255.0f },
};

static void getBodyScale(u8 build, u8 height, float outScale[3])
{
	const float m = 128.0f;
	float x = (build * (height * (0.47f / m) + 0.4f)) / m + height * (0.23f / m) + 0.4f;
	float y = (height * (0.77f / m)) + 0.5f;
	outScale[0] = x;
	outScale[1] = y;
	outScale[2] = x;
}

#define BODY_MESH_IQM_MAGIC "INTERQUAKEMODEL"
#define BODY_MESH_IQM_VERSION 2

typedef struct {
	char magic[16];
	u32 version;
	u32 filesize;
	u32 flags;
	u32 num_text, ofs_text;
	u32 num_meshes, ofs_meshes;
	u32 num_vertexarrays, num_vertexes, ofs_vertexarrays;
	u32 num_triangles, ofs_triangles, ofs_adjacency;
	u32 num_joints, ofs_joints;
	u32 num_poses, ofs_poses;
	u32 num_anims, ofs_anims;
	u32 num_frames, num_framechannels, ofs_frames, ofs_bounds;
	u32 num_comment, ofs_comment;
	u32 num_extensions, ofs_extensions;
} BodyMeshIqmHeader;

typedef struct { u32 name, material; u32 first_vertex, num_vertexes; u32 first_triangle, num_triangles; } BodyMeshIqmMesh;
typedef struct { u32 vertex[3]; } BodyMeshIqmTriangle;
typedef struct { u32 name; s32 parent; float translate[3], rotate[4], scale[3]; } BodyMeshIqmJoint;
typedef struct { s32 parent; u32 mask; float channeloffset[10], channelscale[10]; } BodyMeshIqmPose;
typedef struct { u32 name, first_frame, num_frames; float framerate; u32 flags; } BodyMeshIqmAnim;
typedef struct { u32 type, flags, format, size, offset; } BodyMeshIqmVertexArray;

enum {
	BODY_MESH_IQM_POSITION = 0, BODY_MESH_IQM_TEXCOORD = 1, BODY_MESH_IQM_NORMAL = 2, BODY_MESH_IQM_TANGENT = 3,
	BODY_MESH_IQM_BLENDINDEXES = 4, BODY_MESH_IQM_BLENDWEIGHTS = 5, BODY_MESH_IQM_COLOR = 6, BODY_MESH_IQM_CUSTOM = 0x10,
};
enum {
	BODY_MESH_IQM_BYTE = 0, BODY_MESH_IQM_UBYTE = 1, BODY_MESH_IQM_SHORT = 2, BODY_MESH_IQM_USHORT = 3, BODY_MESH_IQM_INT = 4,
	BODY_MESH_IQM_UINT = 5, BODY_MESH_IQM_HALF = 6, BODY_MESH_IQM_FLOAT = 7, BODY_MESH_IQM_DOUBLE = 8,
};

typedef struct {
	s32 parentBoneIndex;
	float pivot[3];
	u32 category;
	
	const char* name;
} BodyBone;

typedef struct {
	float position[3];
	float normal[3];
	u8 boneIndex[4];
	u8 boneWeight[4];
} BodyRawVertex;

typedef struct {
	u32 materialIndex;
	u32 vertexCount;
	
	BodyRawVertex* vertices;
	u32 indexCount;
	u8* indices;
} BodyPartRaw;

typedef struct {
	u32 nrBones;
	BodyBone bones[BODY_MAX_BONES];
	u32 headBoneIndex;
	u32 partCount;
	BodyPartRaw rawParts[BODY_MAX_PARTS];
	float partColor[BODY_MAX_PARTS][3];
} BodyRig;

typedef struct {
	u32 nrBones;
	BodyBone bones[BODY_MAX_BONES];
	u32 headBoneIndex;
	bool hasHeadBone;
	u32 partCount;
	BodyPartRaw parts[BODY_MAX_PARTS];
} BodyIqmFile;

typedef enum {
	BODY_SCALE_XYZ = 0,
	BODY_SCALE_YXZ = 1,
	BODY_SCALE_SCALAR = 2,
	BODY_SCALE_XYZ_YMIN1 = 3,
	BODY_SCALE_NONE = 4,
} BodyScaleCategory;

static void freeBodyPartRaw(BodyPartRaw* part)
{
	free(part->vertices);
	free(part->indices);
}

static u32 boneCategoryFromName(const char* name)
{
	static const struct { const char* name; u32 category; } kTable[] = {
		{ "arm_l1", BODY_SCALE_YXZ }, { "arm_l2", BODY_SCALE_YXZ },
		{ "arm_r1", BODY_SCALE_YXZ }, { "arm_r2", BODY_SCALE_YXZ },
		{ "wrist_l", BODY_SCALE_SCALAR }, { "wrist_r", BODY_SCALE_SCALAR },
		{ "ankle_l", BODY_SCALE_SCALAR }, { "ankle_r", BODY_SCALE_SCALAR },
		{ "head", BODY_SCALE_XYZ_YMIN1 },
	};
	for (size_t i = 0; i < sizeof(kTable) / sizeof(kTable[0]); i++)
		if (strcmp(name, kTable[i].name) == 0) return kTable[i].category;
	return BODY_SCALE_XYZ;
}

static void bodyQuatToMat3(const float q[4], float m[3][3])
{
	float x = q[0], y = q[1], z = q[2], w = q[3];
	float x2 = x + x, y2 = y + y, z2 = z + z;
	float xx = x * x2, xy = x * y2, xz = x * z2;
	float yy = y * y2, yz = y * z2, zz = z * z2;
	float wx = w * x2, wy = w * y2, wz = w * z2;
	m[0][0] = 1.0f - (yy + zz); m[0][1] = xy - wz;         m[0][2] = xz + wy;
	m[1][0] = xy + wz;          m[1][1] = 1.0f - (xx + zz); m[1][2] = yz - wx;
	m[2][0] = xz - wy;          m[2][1] = yz + wx;          m[2][2] = 1.0f - (xx + yy);
}

typedef struct { float rot[3][3]; float scale[3]; float translate[3]; } BodyJointWorld;

static void composeBodyJointWorld(const BodyMeshIqmJoint* joints, u32 nrJoints, BodyJointWorld* outWorld)
{
	for (u32 i = 0; i < nrJoints; i++) {
		float localRot[3][3];
		bodyQuatToMat3(joints[i].rotate, localRot);
		const float* ls = joints[i].scale;
		const float* lt = joints[i].translate;
		s32 parent = joints[i].parent;

		if (parent < 0) {
			memcpy(outWorld[i].rot, localRot, sizeof(localRot));
			outWorld[i].scale[0] = ls[0]; outWorld[i].scale[1] = ls[1]; outWorld[i].scale[2] = ls[2];
			outWorld[i].translate[0] = lt[0]; outWorld[i].translate[1] = lt[1]; outWorld[i].translate[2] = lt[2];
			continue;
		}
		const BodyJointWorld* pw = &outWorld[parent];
		float scaledT[3] = { lt[0] * pw->scale[0], lt[1] * pw->scale[1], lt[2] * pw->scale[2] };
		for (int r = 0; r < 3; r++) {
			outWorld[i].translate[r] = pw->translate[r]
				+ pw->rot[r][0] * scaledT[0] + pw->rot[r][1] * scaledT[1] + pw->rot[r][2] * scaledT[2];
		}
		for (int r = 0; r < 3; r++) {
			for (int c = 0; c < 3; c++) {
				outWorld[i].rot[r][c] = pw->rot[r][0] * localRot[0][c] + pw->rot[r][1] * localRot[1][c] + pw->rot[r][2] * localRot[2][c];
			}
		}
		
		outWorld[i].scale[0] = pw->scale[0] * ls[0];
		outWorld[i].scale[1] = pw->scale[1] * ls[1];
		outWorld[i].scale[2] = pw->scale[2] * ls[2];
	}
}

static u32 bodyIqmFormatSize(u32 format)
{
	switch (format) {
		case BODY_MESH_IQM_BYTE: case BODY_MESH_IQM_UBYTE: return 1;
		case BODY_MESH_IQM_SHORT: case BODY_MESH_IQM_USHORT: case BODY_MESH_IQM_HALF: return 2;
		case BODY_MESH_IQM_INT: case BODY_MESH_IQM_UINT: case BODY_MESH_IQM_FLOAT: return 4;
		case BODY_MESH_IQM_DOUBLE: return 8;
		default: return 0;
	}
}

static bool splitBottomCircleFromBody(BodyPartRaw* bodyPart, BodyPartRaw* outBase)
{
	if (bodyPart->vertexCount == 0 || bodyPart->indexCount < 3) return false;

	float minY = bodyPart->vertices[0].position[1];
	for (u32 v = 1; v < bodyPart->vertexCount; v++) {
		if (bodyPart->vertices[v].position[1] < minY) minY = bodyPart->vertices[v].position[1];
	}
	const float yLimit = minY + 20.0f;
	const float radiusLimit = 17.0f;

	#define CIRCLE_IS_PANTS(vptr) \
		((vptr)->position[1] < yLimit && \
		 sqrtf((vptr)->position[0] * (vptr)->position[0] + (vptr)->position[2] * (vptr)->position[2]) < radiusLimit)

	u32 triCount = bodyPart->indexCount / 3;
	bool anyBase = false, anyKept = false;
	for (u32 t = 0; t < triCount; t++) {
		bool allLow = true;
		for (int k = 0; k < 3; k++) {
			u8 vi = bodyPart->indices[t * 3 + k];
			if (!CIRCLE_IS_PANTS(&bodyPart->vertices[vi])) { allLow = false; break; }
		}
		if (allLow) anyBase = true; else anyKept = true;
	}
	if (!anyBase) return false;

	s16 remapBase[256], remapKept[256];
	for (int i = 0; i < 256; i++) { remapBase[i] = -1; remapKept[i] = -1; }

	BodyRawVertex* baseVerts = malloc(sizeof(BodyRawVertex) * bodyPart->vertexCount);
	u8* baseIndices = malloc(bodyPart->indexCount);
	BodyRawVertex* keptVerts = anyKept ? malloc(sizeof(BodyRawVertex) * bodyPart->vertexCount) : NULL;
	u8* keptIndices = anyKept ? malloc(bodyPart->indexCount) : NULL;
	if (!baseVerts || !baseIndices || (anyKept && (!keptVerts || !keptIndices))) {
		free(baseVerts); free(baseIndices); free(keptVerts); free(keptIndices);
		return false;
	}
	u32 baseVertCount = 0, baseIndexCount = 0, keptVertCount = 0, keptIndexCount = 0;

	for (u32 t = 0; t < triCount; t++) {
		u8 srcIdx[3] = { bodyPart->indices[t*3+0], bodyPart->indices[t*3+1], bodyPart->indices[t*3+2] };
		bool allLow = true;
		for (int k = 0; k < 3; k++) {
			if (!CIRCLE_IS_PANTS(&bodyPart->vertices[srcIdx[k]])) { allLow = false; break; }
		}
		if (allLow) {
			for (int k = 0; k < 3; k++) {
				if (remapBase[srcIdx[k]] < 0) { remapBase[srcIdx[k]] = (s16)baseVertCount; baseVerts[baseVertCount++] = bodyPart->vertices[srcIdx[k]]; }
				baseIndices[baseIndexCount++] = (u8)remapBase[srcIdx[k]];
			}
		} else {
			for (int k = 0; k < 3; k++) {
				if (remapKept[srcIdx[k]] < 0) { remapKept[srcIdx[k]] = (s16)keptVertCount; keptVerts[keptVertCount++] = bodyPart->vertices[srcIdx[k]]; }
				keptIndices[keptIndexCount++] = (u8)remapKept[srcIdx[k]];
			}
		}
	}

	outBase->materialIndex = 1;
	outBase->vertexCount = baseVertCount;
	outBase->vertices = baseVerts;
	outBase->indexCount = baseIndexCount;
	outBase->indices = baseIndices;

	free(bodyPart->vertices);
	free(bodyPart->indices);
	if (anyKept) {
		bodyPart->vertexCount = keptVertCount;
		bodyPart->vertices = keptVerts;
		bodyPart->indexCount = keptIndexCount;
		bodyPart->indices = keptIndices;
	} else {
		
		bodyPart->vertexCount = 0;
		bodyPart->vertices = NULL;
		bodyPart->indexCount = 0;
		bodyPart->indices = NULL;
	}
	dbglog("splitBottomCircleFromBody: split %lu base tri(s) (%lu vert(s)) from %lu kept tri(s) (%lu vert(s))\n",
		(unsigned long)(baseIndexCount/3), (unsigned long)baseVertCount, (unsigned long)(keptIndexCount/3), (unsigned long)keptVertCount);
	#undef CIRCLE_IS_PANTS
	return true;
}

static bool parseBodyIqm(const u8* data, u32 size, BodyIqmFile* out)
{
	memset(out, 0, sizeof(*out));
	if (size < sizeof(BodyMeshIqmHeader)) { dbglog("parseBodyIqm: file too small for header\n"); return false; }
	BodyMeshIqmHeader hdr;
	memcpy(&hdr, data, sizeof(hdr));
	if (memcmp(hdr.magic, BODY_MESH_IQM_MAGIC, 16) != 0) { dbglog("parseBodyIqm: bad magic\n"); return false; }
	if (hdr.version != BODY_MESH_IQM_VERSION) { dbglog("parseBodyIqm: unsupported version %lu\n", (unsigned long)hdr.version); return false; }
	if (hdr.filesize > size) { dbglog("parseBodyIqm: header filesize %lu exceeds buffer %lu\n", (unsigned long)hdr.filesize, (unsigned long)size); return false; }

	#define BODY_MESH_IQM_FITS(ofs, count, itemSize) ((u64)(ofs) + (u64)(count) * (u64)(itemSize) <= size)

	const char* text = NULL;
	if (hdr.num_text) {
		if (!BODY_MESH_IQM_FITS(hdr.ofs_text, hdr.num_text, 1)) { dbglog("parseBodyIqm: text section out of bounds\n"); return false; }
		text = (const char*)(data + hdr.ofs_text);
	}

	if (hdr.num_joints > BODY_MAX_BONES) { dbglog("parseBodyIqm: num_joints %lu exceeds BODY_MAX_BONES\n", (unsigned long)hdr.num_joints); return false; }
	if (!BODY_MESH_IQM_FITS(hdr.ofs_joints, hdr.num_joints, sizeof(BodyMeshIqmJoint))) { dbglog("parseBodyIqm: joints out of bounds\n"); return false; }
	BodyMeshIqmJoint joints[BODY_MAX_BONES];
	memcpy(joints, data + hdr.ofs_joints, (size_t)hdr.num_joints * sizeof(BodyMeshIqmJoint));
	for (u32 i = 0; i < hdr.num_joints; i++) {
		if (joints[i].parent >= (s32)i) { dbglog("parseBodyIqm: joint %lu parent index %ld not strictly before it\n", (unsigned long)i, (long)joints[i].parent); return false; }
	}

	BodyJointWorld world[BODY_MAX_BONES];
	composeBodyJointWorld(joints, hdr.num_joints, world);

	out->nrBones = hdr.num_joints;
	out->headBoneIndex = 0;
	bool foundHead = false;
	for (u32 i = 0; i < hdr.num_joints; i++) {
		out->bones[i].parentBoneIndex = joints[i].parent;
		out->bones[i].pivot[0] = world[i].translate[0];
		out->bones[i].pivot[1] = world[i].translate[1];
		out->bones[i].pivot[2] = world[i].translate[2];
		const char* name = (text && joints[i].name < hdr.num_text) ? (text + joints[i].name) : "";
		out->bones[i].category = boneCategoryFromName(name);
		out->bones[i].name = name;
		
		if (!foundHead && (strcmp(name, "head") == 0 || strcmp(name, "headPs") == 0)) { out->headBoneIndex = i; foundHead = true; }
	}
	out->hasHeadBone = foundHead;
	if (!foundHead) dbglog("parseBodyIqm: no joint named \"head\"/\"headPs\" found - this body has no head attach point\n");

	if (!BODY_MESH_IQM_FITS(hdr.ofs_vertexarrays, hdr.num_vertexarrays, sizeof(BodyMeshIqmVertexArray))) { dbglog("parseBodyIqm: vertex arrays out of bounds\n"); return false; }
	const float* vaPosition = NULL;
	const float* vaNormal = NULL;
	const u8* vaBlendIndexes = NULL;
	const u8* vaBlendWeights = NULL;
	for (u32 i = 0; i < hdr.num_vertexarrays; i++) {
		BodyMeshIqmVertexArray va;
		memcpy(&va, data + hdr.ofs_vertexarrays + (size_t)i * sizeof(va), sizeof(va));
		u32 itemSize = bodyIqmFormatSize(va.format);
		if (itemSize == 0 || !BODY_MESH_IQM_FITS(va.offset, hdr.num_vertexes, (u64)va.size * itemSize)) {
			
			if (va.type == BODY_MESH_IQM_POSITION || va.type == BODY_MESH_IQM_NORMAL || va.type == BODY_MESH_IQM_BLENDINDEXES || va.type == BODY_MESH_IQM_BLENDWEIGHTS) {
				dbglog("parseBodyIqm: required vertex array type=%lu out of bounds or bad format\n", (unsigned long)va.type);
				return false;
			}
			continue;
		}
		switch (va.type) {
			case BODY_MESH_IQM_POSITION:
				if (va.format != BODY_MESH_IQM_FLOAT || va.size != 3) { dbglog("parseBodyIqm: position array must be FLOAT x3\n"); return false; }
				vaPosition = (const float*)(data + va.offset);
				break;
			case BODY_MESH_IQM_NORMAL:
				if (va.format != BODY_MESH_IQM_FLOAT || va.size != 3) { dbglog("parseBodyIqm: normal array must be FLOAT x3\n"); return false; }
				vaNormal = (const float*)(data + va.offset);
				break;
			case BODY_MESH_IQM_BLENDINDEXES:
				if (va.format != BODY_MESH_IQM_UBYTE || va.size != 4) { dbglog("parseBodyIqm: blendindexes array must be UBYTE x4\n"); return false; }
				vaBlendIndexes = data + va.offset;
				break;
			case BODY_MESH_IQM_BLENDWEIGHTS:
				if (va.format != BODY_MESH_IQM_UBYTE || va.size != 4) { dbglog("parseBodyIqm: blendweights array must be UBYTE x4\n"); return false; }
				vaBlendWeights = data + va.offset;
				break;
			default:
				break;
		}
	}
	if (!vaPosition || !vaNormal || !vaBlendIndexes || !vaBlendWeights) {
		dbglog("parseBodyIqm: missing a required vertex array (position/normal/blendindexes/blendweights)\n");
		return false;
	}

	if (hdr.num_meshes > BODY_MAX_PARTS) { dbglog("parseBodyIqm: num_meshes %lu exceeds BODY_MAX_PARTS\n", (unsigned long)hdr.num_meshes); return false; }
	if (!BODY_MESH_IQM_FITS(hdr.ofs_meshes, hdr.num_meshes, sizeof(BodyMeshIqmMesh))) { dbglog("parseBodyIqm: meshes out of bounds\n"); return false; }
	if (!BODY_MESH_IQM_FITS(hdr.ofs_triangles, hdr.num_triangles, sizeof(BodyMeshIqmTriangle))) { dbglog("parseBodyIqm: triangles out of bounds\n"); return false; }

	out->partCount = hdr.num_meshes;
	for (u32 m = 0; m < hdr.num_meshes; m++) {
		BodyMeshIqmMesh mesh;
		memcpy(&mesh, data + hdr.ofs_meshes + (size_t)m * sizeof(mesh), sizeof(mesh));
		if ((u64)mesh.first_vertex + mesh.num_vertexes > hdr.num_vertexes) { dbglog("parseBodyIqm: mesh %lu vertex range out of bounds\n", (unsigned long)m); return false; }
		if ((u64)mesh.first_triangle + mesh.num_triangles > hdr.num_triangles) { dbglog("parseBodyIqm: mesh %lu triangle range out of bounds\n", (unsigned long)m); return false; }
		if (mesh.num_vertexes > 256) { dbglog("parseBodyIqm: mesh %lu has %lu verts, exceeds this project's own u8 index limit\n", (unsigned long)m, (unsigned long)mesh.num_vertexes); return false; }

		if (mesh.material >= hdr.num_text) { dbglog("parseBodyIqm: mesh %lu material text offset out of bounds\n", (unsigned long)m); return false; }
		const char* materialName = text + mesh.material;
		
		u32 materialIndex = (strcmp(materialName, "mt_pants") == 0) ? 1 : 0;

		BodyPartRaw* part = &out->parts[m];
		part->materialIndex = materialIndex;
		part->vertexCount = mesh.num_vertexes;
		part->vertices = malloc(sizeof(BodyRawVertex) * mesh.num_vertexes);
		if (!part->vertices) return false;
		for (u32 v = 0; v < mesh.num_vertexes; v++) {
			u32 srcV = mesh.first_vertex + v;
			BodyRawVertex* dst = &part->vertices[v];
			dst->position[0] = vaPosition[srcV * 3 + 0]; dst->position[1] = vaPosition[srcV * 3 + 1]; dst->position[2] = vaPosition[srcV * 3 + 2];
			dst->normal[0] = vaNormal[srcV * 3 + 0]; dst->normal[1] = vaNormal[srcV * 3 + 1]; dst->normal[2] = vaNormal[srcV * 3 + 2];
			for (int k = 0; k < 4; k++) {
				dst->boneIndex[k] = vaBlendIndexes[srcV * 4 + k];
				dst->boneWeight[k] = vaBlendWeights[srcV * 4 + k];
			}
		}

		part->indexCount = mesh.num_triangles * 3;
		part->indices = malloc(part->indexCount);
		if (!part->indices) { free(part->vertices); return false; }
		for (u32 t = 0; t < mesh.num_triangles; t++) {
			BodyMeshIqmTriangle tri;
			memcpy(&tri, data + hdr.ofs_triangles + (size_t)(mesh.first_triangle + t) * sizeof(tri), sizeof(tri));
			for (int k = 0; k < 3; k++) {
				if (tri.vertex[k] < mesh.first_vertex || tri.vertex[k] >= mesh.first_vertex + mesh.num_vertexes) {
					dbglog("parseBodyIqm: mesh %lu triangle %lu references a vertex outside its own mesh\n", (unsigned long)m, (unsigned long)t);
					free(part->indices); free(part->vertices);
					return false;
				}
				part->indices[t * 3 + k] = (u8)(tri.vertex[k] - mesh.first_vertex);
			}
		}
	}

	{
		bool hasPants = false;
		for (u32 i = 0; i < out->partCount; i++) if (out->parts[i].materialIndex == 1) hasPants = true;
		if (!hasPants && out->partCount < BODY_MAX_PARTS) {
			s32 biggestBody = -1;
			u32 biggestCount = 0;
			for (u32 i = 0; i < out->partCount; i++) {
				if (out->parts[i].materialIndex == 0 && out->parts[i].vertexCount > biggestCount) {
					biggestCount = out->parts[i].vertexCount;
					biggestBody = (s32)i;
				}
			}
			if (biggestBody >= 0) {
				BodyPartRaw base;
				memset(&base, 0, sizeof(base));
				if (splitBottomCircleFromBody(&out->parts[biggestBody], &base)) {
					out->parts[out->partCount] = base;
					out->partCount++;
				}
			}
		}
	}

	if (hdr.num_poses && !BODY_MESH_IQM_FITS(hdr.ofs_poses, hdr.num_poses, sizeof(BodyMeshIqmPose))) { dbglog("parseBodyIqm: poses out of bounds\n"); return false; }
	if (hdr.num_anims && !BODY_MESH_IQM_FITS(hdr.ofs_anims, hdr.num_anims, sizeof(BodyMeshIqmAnim))) { dbglog("parseBodyIqm: anims out of bounds\n"); return false; }
	if (hdr.num_frames && hdr.num_framechannels && !BODY_MESH_IQM_FITS(hdr.ofs_frames, (u64)hdr.num_frames * hdr.num_framechannels, sizeof(u16))) { dbglog("parseBodyIqm: frames out of bounds\n"); return false; }
	if (hdr.num_poses && !BODY_MESH_IQM_FITS(hdr.ofs_bounds, hdr.num_frames, sizeof(float) * 8)) {  }

	dbglog("parseBodyIqm: parsed OK - %lu joint(s), %lu mesh(es), %lu pose(s), %lu anim(s), %lu frame(s)\n",
		(unsigned long)hdr.num_joints, (unsigned long)hdr.num_meshes, (unsigned long)hdr.num_poses, (unsigned long)hdr.num_anims, (unsigned long)hdr.num_frames);
	return true;

	#undef BODY_MESH_IQM_FITS
}

static void boneCategoryScale(u32 category, const float bodyScale[3], float out[3])
{
	switch (category) {
		case BODY_SCALE_XYZ:
			out[0] = bodyScale[0]; out[1] = bodyScale[1]; out[2] = bodyScale[2];
			break;
		case BODY_SCALE_YXZ:
			out[0] = bodyScale[1]; out[1] = bodyScale[0]; out[2] = bodyScale[2];
			break;
		case BODY_SCALE_SCALAR:
			out[0] = out[1] = out[2] = bodyScale[0];
			break;
		case BODY_SCALE_XYZ_YMIN1:
			out[0] = bodyScale[0]; out[1] = bodyScale[1] > 1.0f ? bodyScale[1] : 1.0f; out[2] = bodyScale[2];
			break;
		case BODY_SCALE_NONE:
		default:
			out[0] = out[1] = out[2] = 1.0f;
			break;
	}
}

static void computeScaledBoneTransforms(const BodyBone* bones, u32 nrBones,
	const float unscaledWorldPos[BODY_MAX_BONES][3], const float bodyScale[3],
	float outWorldPivot[BODY_MAX_BONES][3], float outOwnScale[BODY_MAX_BONES][3])
{
	for (u32 i = 0; i < nrBones; i++) {
		boneCategoryScale(bones[i].category, bodyScale, outOwnScale[i]);
		s32 parent = bones[i].parentBoneIndex;
		if (parent < 0) {
			outWorldPivot[i][0] = unscaledWorldPos[i][0];
			outWorldPivot[i][1] = unscaledWorldPos[i][1];
			outWorldPivot[i][2] = unscaledWorldPos[i][2];
		} else {
			const float* parentPivot = outWorldPivot[parent];
			const float* parentScale = outOwnScale[parent];
			float localOffset[3] = {
				unscaledWorldPos[i][0] - unscaledWorldPos[parent][0],
				unscaledWorldPos[i][1] - unscaledWorldPos[parent][1],
				unscaledWorldPos[i][2] - unscaledWorldPos[parent][2],
			};
			outWorldPivot[i][0] = parentPivot[0] + parentScale[0] * localOffset[0];
			outWorldPivot[i][1] = parentPivot[1] + parentScale[1] * localOffset[1];
			outWorldPivot[i][2] = parentPivot[2] + parentScale[2] * localOffset[2];
		}
	}
}

static void bodyMat3MulVec3(const float m[3][3], const float v[3], float out[3])
{
	out[0] = m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2];
	out[1] = m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2];
	out[2] = m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2];
}

static bool skinBodyPart(BodyPart* outPart, const BodyPartRaw* srcPart,
	const float worldPivot[BODY_MAX_BONES][3], const float ownScale[BODY_MAX_BONES][3],
	const float rot[BODY_MAX_BONES][3][3], const BodyBone* bones, const float color[3])
{
	float* positions = malloc(sizeof(float) * 3 * srcPart->vertexCount);
	float* normals = malloc(sizeof(float) * 3 * srcPart->vertexCount);
	if (!positions || !normals) { free(positions); free(normals); return false; }

	for (u32 v = 0; v < srcPart->vertexCount; v++) {
		const BodyRawVertex* rv = &srcPart->vertices[v];
		float pos[3] = { 0.0f, 0.0f, 0.0f };
		float nrm[3] = { 0.0f, 0.0f, 0.0f };
		u32 totalWeight = 0;
		for (int k = 0; k < 4; k++) {
			u32 w = rv->boneWeight[k];
			if (w == 0) continue;
			u32 b = rv->boneIndex[k];
			const float* piv = bones[b].pivot;
			const float* wp = worldPivot[b];
			const float* cs = ownScale[b];
			float wf = (float)w / 255.0f;

			float scaledOffset[3] = {
				cs[0] * (rv->position[0] - piv[0]),
				cs[1] * (rv->position[1] - piv[1]),
				cs[2] * (rv->position[2] - piv[2]),
			};
			float rotatedOffset[3];
			if (rot) bodyMat3MulVec3(rot[b], scaledOffset, rotatedOffset);
			else { rotatedOffset[0] = scaledOffset[0]; rotatedOffset[1] = scaledOffset[1]; rotatedOffset[2] = scaledOffset[2]; }
			for (int c = 0; c < 3; c++)
				pos[c] += wf * (wp[c] + rotatedOffset[c]);

			float scaledNrm[3] = { rv->normal[0] / cs[0], rv->normal[1] / cs[1], rv->normal[2] / cs[2] };
			float rotatedNrm[3];
			if (rot) bodyMat3MulVec3(rot[b], scaledNrm, rotatedNrm);
			else { rotatedNrm[0] = scaledNrm[0]; rotatedNrm[1] = scaledNrm[1]; rotatedNrm[2] = scaledNrm[2]; }
			nrm[0] += wf * rotatedNrm[0]; nrm[1] += wf * rotatedNrm[1]; nrm[2] += wf * rotatedNrm[2];
			totalWeight += w;
		}
		if (totalWeight == 0) {
			
			const float* piv = bones[0].pivot;
			const float* wp = worldPivot[0];
			const float* cs = ownScale[0];
			float scaledOffset[3] = { cs[0] * (rv->position[0] - piv[0]), cs[1] * (rv->position[1] - piv[1]), cs[2] * (rv->position[2] - piv[2]) };
			float rotatedOffset[3];
			if (rot) bodyMat3MulVec3(rot[0], scaledOffset, rotatedOffset);
			else { rotatedOffset[0] = scaledOffset[0]; rotatedOffset[1] = scaledOffset[1]; rotatedOffset[2] = scaledOffset[2]; }
			for (int c = 0; c < 3; c++) pos[c] = wp[c] + rotatedOffset[c];
			float scaledNrm[3] = { rv->normal[0] / cs[0], rv->normal[1] / cs[1], rv->normal[2] / cs[2] };
			if (rot) bodyMat3MulVec3(rot[0], scaledNrm, nrm);
			else { nrm[0] = scaledNrm[0]; nrm[1] = scaledNrm[1]; nrm[2] = scaledNrm[2]; }
		}
		float len = sqrtf(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
		if (len > 1e-8f) { nrm[0] /= len; nrm[1] /= len; nrm[2] /= len; }
		positions[v * 3 + 0] = pos[0]; positions[v * 3 + 1] = pos[1]; positions[v * 3 + 2] = pos[2];
		normals[v * 3 + 0] = nrm[0]; normals[v * 3 + 1] = nrm[1]; normals[v * 3 + 2] = nrm[2];
	}

	Vertex* newVertsTyped = linearAlloc(sizeof(Vertex) * srcPart->vertexCount);
	for (u32 vi = 0; vi < srcPart->vertexCount; vi++) {
		newVertsTyped[vi].position[0] = positions[vi*3+0];
		newVertsTyped[vi].position[1] = positions[vi*3+1];
		newVertsTyped[vi].position[2] = positions[vi*3+2];
		newVertsTyped[vi].normal[0] = normals[vi*3+0];
		newVertsTyped[vi].normal[1] = normals[vi*3+1];
		newVertsTyped[vi].normal[2] = normals[vi*3+2];
		newVertsTyped[vi].texcoord[0] = 0.0f;
		newVertsTyped[vi].texcoord[1] = 0.0f;
	}
	void* newVbo = newVertsTyped;
	u32 vcount = srcPart->vertexCount;
	void* newIbo = linearAlloc(srcPart->indexCount);
	memcpy(newIbo, srcPart->indices, srcPart->indexCount);

	if (outPart->vbo) linearFree(outPart->vbo);
	if (outPart->ibo) linearFree(outPart->ibo);
	outPart->vbo = newVbo;
	outPart->vertexCount = vcount;
	outPart->ibo = newIbo;
	outPart->indexCount = srcPart->indexCount;
	outPart->color[0] = color[0];
	outPart->color[1] = color[1];
	outPart->color[2] = color[2];

	free(positions);
	free(normals);
	return true;
}

bool loadBodyModel(const u8* bodyData, u32 bodySize, const MiiData* mii, bool applyMiiScale, BodyModel* outBody)
{
	dbglog("loadBodyModel: start (bodySize=%lu, applyMiiScale=%d)\n", (unsigned long)bodySize, applyMiiScale);

	memset(outBody, 0, sizeof(*outBody));
	if (!bodyData || !mii) return false;

	BodyIqmFile file;
	if (!parseBodyIqm(bodyData, bodySize, &file)) return false;

	if (applyMiiScale) getBodyScale(mii->width, mii->height, outBody->bodyScale);
	else { outBody->bodyScale[0] = 1.0f; outBody->bodyScale[1] = 1.0f; outBody->bodyScale[2] = 1.0f; }

	outBody->headWorldScale = applyMiiScale ? 1.0f : 0.8675f;

	float bindPos[BODY_MAX_BONES][3];
	for (u32 i = 0; i < file.nrBones; i++) {
		bindPos[i][0] = file.bones[i].pivot[0];
		bindPos[i][1] = file.bones[i].pivot[1];
		bindPos[i][2] = file.bones[i].pivot[2];
	}
	float worldPivot[BODY_MAX_BONES][3];
	float ownScale[BODY_MAX_BONES][3];
	computeScaledBoneTransforms(file.bones, file.nrBones, bindPos, outBody->bodyScale, worldPivot, ownScale);

	if (file.hasHeadBone) {
		outBody->hasHeadBone = true;
		outBody->headBoneWorldMatrix[0] = 1.0f; outBody->headBoneWorldMatrix[1] = 0.0f; outBody->headBoneWorldMatrix[2] = 0.0f;
		outBody->headBoneWorldMatrix[3] = worldPivot[file.headBoneIndex][0];
		outBody->headBoneWorldMatrix[4] = 0.0f; outBody->headBoneWorldMatrix[5] = 1.0f; outBody->headBoneWorldMatrix[6] = 0.0f;
		outBody->headBoneWorldMatrix[7] = worldPivot[file.headBoneIndex][1];
		outBody->headBoneWorldMatrix[8] = 0.0f; outBody->headBoneWorldMatrix[9] = 0.0f; outBody->headBoneWorldMatrix[10] = 1.0f;
		outBody->headBoneWorldMatrix[11] = worldPivot[file.headBoneIndex][2];
	}

	const u8* miiBytes = (const u8*)mii;
	bool isSpecialMii = (miiBytes[12] & 0x80) == 0;
	const float* pantsColor = pantsColors[isSpecialMii ? 3 : 0];
	const float* bodyColor = CFL_GetFavoriteColor(mii->mii_details.shirt_color);

	BodyRig* rig = malloc(sizeof(BodyRig));
	if (rig) {
		rig->nrBones = file.nrBones;
		memcpy(rig->bones, file.bones, sizeof(BodyBone) * file.nrBones);
		rig->headBoneIndex = file.headBoneIndex;
		rig->partCount = 0;
	}

	for (u32 i = 0; i < file.partCount; i++) {
		BodyPartRaw* part = &file.parts[i];
		const float* color = part->materialIndex == 0 ? bodyColor : pantsColor;
		if (skinBodyPart(&outBody->parts[outBody->partCount], part, worldPivot, ownScale, NULL, file.bones, color)) {
			if (rig) {
				
				rig->rawParts[rig->partCount] = *part;
				rig->partColor[rig->partCount][0] = color[0];
				rig->partColor[rig->partCount][1] = color[1];
				rig->partColor[rig->partCount][2] = color[2];
				rig->partCount++;
			} else {
				freeBodyPartRaw(part);
			}
			outBody->partCount++;
		} else {
			freeBodyPartRaw(part);
		}
	}

	if (outBody->partCount == 0) {
		dbglog("loadBodyModel: no body/pants parts were successfully loaded\n");
		free(rig);
		return false;
	}
	if (rig && rig->partCount != outBody->partCount) {
		
		dbglog("loadBodyModel: rig part count mismatch, disabling animation playback for this body\n");
		for (u32 i = 0; i < rig->partCount; i++) freeBodyPartRaw(&rig->rawParts[i]);
		free(rig);
		rig = NULL;
	}
	outBody->rig = rig;
	if (!rig) dbglog("loadBodyModel: no rig retained - poseBodyModel will not work for this body\n");

	dbglog("loadBodyModel: done, %lu part(s), hasHeadBone=%d, head world pos=(%.2f,%.2f,%.2f), rig=%s\n",
		(unsigned long)outBody->partCount, outBody->hasHeadBone,
		outBody->headBoneWorldMatrix[3], outBody->headBoneWorldMatrix[7], outBody->headBoneWorldMatrix[11],
		rig ? "yes" : "no");
	return true;
}

void deleteBodyModel(BodyModel* body)
{
	for (int i = 0; i < body->partCount; i++) {
		if (body->parts[i].vbo) linearFree(body->parts[i].vbo);
		if (body->parts[i].ibo) linearFree(body->parts[i].ibo);
	}
	if (body->rig) {
		BodyRig* rig = (BodyRig*)body->rig;
		for (u32 i = 0; i < rig->partCount; i++) freeBodyPartRaw(&rig->rawParts[i]);
		free(rig);
	}
	memset(body, 0, sizeof(*body));
}

u32 getBodyBoneCount(const BodyModel* body)
{
	if (!body || !body->rig) return 0;
	return ((const BodyRig*)body->rig)->nrBones;
}

const char* getBodyBoneName(const BodyModel* body, u32 boneIndex)
{
	if (!body || !body->rig) return NULL;
	const BodyRig* rig = (const BodyRig*)body->rig;
	if (boneIndex >= rig->nrBones) return NULL;
	return rig->bones[boneIndex].name;
}

static void computeBindLocalPose(const BodyRig* rig, u32 i, BoneLocalPose* out)
{
	s32 parent = rig->bones[i].parentBoneIndex;
	const float* piv = rig->bones[i].pivot;
	if (parent < 0) {
		out->translate[0] = piv[0]; out->translate[1] = piv[1]; out->translate[2] = piv[2];
	} else {
		const float* parentPiv = rig->bones[parent].pivot;
		out->translate[0] = piv[0] - parentPiv[0];
		out->translate[1] = piv[1] - parentPiv[1];
		out->translate[2] = piv[2] - parentPiv[2];
	}
	out->rotate[0] = 0.0f; out->rotate[1] = 0.0f; out->rotate[2] = 0.0f; out->rotate[3] = 1.0f;
	out->scale[0] = out->scale[1] = out->scale[2] = 1.0f;
}

bool getBodyBoneBindLocalPose(const BodyModel* body, u32 boneIndex, BoneLocalPose* outPose)
{
	if (!body || !body->rig || !outPose) return false;
	const BodyRig* rig = (const BodyRig*)body->rig;
	if (boneIndex >= rig->nrBones) return false;
	computeBindLocalPose(rig, boneIndex, outPose);
	return true;
}

bool poseBodyModel(BodyModel* body, const BoneLocalPose* poses, u32 boneCount)
{
	if (!body || !body->rig) return false;
	BodyRig* rig = (BodyRig*)body->rig;
	if (poses && boneCount != rig->nrBones) return false;

	BodyMeshIqmJoint localJoints[BODY_MAX_BONES];
	memset(localJoints, 0, sizeof(localJoints));
	for (u32 i = 0; i < rig->nrBones; i++) {
		localJoints[i].parent = rig->bones[i].parentBoneIndex;
		if (poses) {
			localJoints[i].translate[0] = poses[i].translate[0];
			localJoints[i].translate[1] = poses[i].translate[1];
			localJoints[i].translate[2] = poses[i].translate[2];
			localJoints[i].rotate[0] = poses[i].rotate[0];
			localJoints[i].rotate[1] = poses[i].rotate[1];
			localJoints[i].rotate[2] = poses[i].rotate[2];
			localJoints[i].rotate[3] = poses[i].rotate[3];
			localJoints[i].scale[0] = poses[i].scale[0];
			localJoints[i].scale[1] = poses[i].scale[1];
			localJoints[i].scale[2] = poses[i].scale[2];
		} else {
			BoneLocalPose bind;
			computeBindLocalPose(rig, i, &bind);
			memcpy(localJoints[i].translate, bind.translate, sizeof(bind.translate));
			memcpy(localJoints[i].rotate, bind.rotate, sizeof(bind.rotate));
			memcpy(localJoints[i].scale, bind.scale, sizeof(bind.scale));
		}
	}

	BodyJointWorld animWorld[BODY_MAX_BONES];
	composeBodyJointWorld(localJoints, rig->nrBones, animWorld);

	float animPos[BODY_MAX_BONES][3];
	float animRot[BODY_MAX_BONES][3][3];
	for (u32 i = 0; i < rig->nrBones; i++) {
		animPos[i][0] = animWorld[i].translate[0];
		animPos[i][1] = animWorld[i].translate[1];
		animPos[i][2] = animWorld[i].translate[2];
		memcpy(animRot[i], animWorld[i].rot, sizeof(animRot[i]));
	}

	float worldPivot[BODY_MAX_BONES][3];
	float ownScale[BODY_MAX_BONES][3];
	computeScaledBoneTransforms(rig->bones, rig->nrBones, animPos, body->bodyScale, worldPivot, ownScale);

	for (u32 i = 0; i < rig->partCount; i++) {
		skinBodyPart(&body->parts[i], &rig->rawParts[i], worldPivot, ownScale, animRot, rig->bones, rig->partColor[i]);
	}

	if (body->hasHeadBone && rig->headBoneIndex < rig->nrBones) {
		u32 h = rig->headBoneIndex;
		const float (*r)[3] = animRot[h];
		body->headBoneWorldMatrix[0] = r[0][0]; body->headBoneWorldMatrix[1] = r[0][1]; body->headBoneWorldMatrix[2] = r[0][2];
		body->headBoneWorldMatrix[3] = worldPivot[h][0];
		body->headBoneWorldMatrix[4] = r[1][0]; body->headBoneWorldMatrix[5] = r[1][1]; body->headBoneWorldMatrix[6] = r[1][2];
		body->headBoneWorldMatrix[7] = worldPivot[h][1];
		body->headBoneWorldMatrix[8] = r[2][0]; body->headBoneWorldMatrix[9] = r[2][1]; body->headBoneWorldMatrix[10] = r[2][2];
		body->headBoneWorldMatrix[11] = worldPivot[h][2];
	}
	return true;
}

static C3D_Mtx projection;

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
	SCREEN_DATA_TEST,
	SCREEN_BODY_TEST,
} AppScreen;
static AppScreen screen = SCREEN_TITLE;

#define TITLE_OPTION_COUNT 4
static const char* kTitleOptions[TITLE_OPTION_COUNT] = {
	"CharModel only Test",
	"CharModel + Body Test",
	"Icon Test",
	"CharModel from Data",
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
static C3D_Tex iconTexture256NoBody;
static C3D_Tex iconTexture128NoBody;
static bool iconTexture256NoBodyValid = false;
static bool iconTexture128NoBodyValid = false;
static C3D_Tex iconTexture256Body;
static C3D_Tex iconTexture128Body;
static bool iconTexture256BodyValid = false;
static bool iconTexture128BodyValid = false;
static bool iconShowBody = false;

static void releaseIconTextures(void)
{
	if (iconTexture256NoBodyValid) { C3D_TexDelete(&iconTexture256NoBody); iconTexture256NoBodyValid = false; }
	if (iconTexture128NoBodyValid) { C3D_TexDelete(&iconTexture128NoBody); iconTexture128NoBodyValid = false; }
	if (iconTexture256BodyValid) { C3D_TexDelete(&iconTexture256Body); iconTexture256BodyValid = false; }
	if (iconTexture128BodyValid) { C3D_TexDelete(&iconTexture128Body); iconTexture128BodyValid = false; }
}

static CFLCharModel bodyTestModel;
static bool bodyTestModelValid = false;
static BodyModel bodyTestBody;
static bool bodyTestBodyValid = false;
static float bodySpin = 0.0f;

typedef enum { BODY_SOURCE_MII_MAKER, BODY_SOURCE_STREETPASS } BodySource;
static BodySource bodyTestSource = BODY_SOURCE_MII_MAKER;
static MiiData bodyTestMii;

#define BODY_ANIM_MAX_JOINTS BODY_MAX_BONES

typedef struct {
	u32 jointCount;
	char jointNames[BODY_ANIM_MAX_JOINTS][32];
	u32 frameCount;
	float framerate;
	BoneLocalPose* framePoses;
} BodyAnimClip;

typedef struct {
	char magic[16];
	u32 version;
	u32 filesize;
	u32 flags;
	u32 num_text, ofs_text;
	u32 num_meshes, ofs_meshes;
	u32 num_vertexarrays, num_vertexes, ofs_vertexarrays;
	u32 num_triangles, ofs_triangles, ofs_adjacency;
	u32 num_joints, ofs_joints;
	u32 num_poses, ofs_poses;
	u32 num_anims, ofs_anims;
	u32 num_frames, num_framechannels, ofs_frames, ofs_bounds;
	u32 num_comment, ofs_comment;
	u32 num_extensions, ofs_extensions;
} BodyAnimIqmHeader;
typedef struct { u32 name; s32 parent; float translate[3], rotate[4], scale[3]; } BodyAnimIqmJoint;
typedef struct { s32 parent; u32 mask; float channeloffset[10], channelscale[10]; } BodyAnimIqmPose;
typedef struct { u32 name, first_frame, num_frames; float framerate; u32 flags; } BodyAnimIqmAnimHdr;

static void bodyAnimQuatNormalize(float q[4])
{
	float len = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
	if (len > 1e-8f) { q[0] /= len; q[1] /= len; q[2] /= len; q[3] /= len; }
	else { q[0] = 0.0f; q[1] = 0.0f; q[2] = 0.0f; q[3] = 1.0f; }
}

static bool loadBodyAnimClip(const u8* data, u32 size, BodyAnimClip* out)
{
	memset(out, 0, sizeof(*out));
	if (size < sizeof(BodyAnimIqmHeader)) { dbglog("loadBodyAnimClip: file too small for header\n"); return false; }
	BodyAnimIqmHeader hdr;
	memcpy(&hdr, data, sizeof(hdr));
	if (memcmp(hdr.magic, "INTERQUAKEMODEL\0", 16) != 0) { dbglog("loadBodyAnimClip: bad magic\n"); return false; }
	if (hdr.version != 2) { dbglog("loadBodyAnimClip: unsupported version %lu\n", (unsigned long)hdr.version); return false; }
	if (hdr.filesize > size) { dbglog("loadBodyAnimClip: header filesize %lu exceeds buffer %lu\n", (unsigned long)hdr.filesize, (unsigned long)size); return false; }

	#define BANIM_FITS(ofs, count, itemSize) ((u64)(ofs) + (u64)(count) * (u64)(itemSize) <= size)

	const char* text = NULL;
	if (hdr.num_text) {
		if (!BANIM_FITS(hdr.ofs_text, hdr.num_text, 1)) { dbglog("loadBodyAnimClip: text section out of bounds\n"); return false; }
		text = (const char*)(data + hdr.ofs_text);
	}

	if (hdr.num_joints == 0) { dbglog("loadBodyAnimClip: no joints - not a skeleton\n"); return false; }
	if (hdr.num_joints > BODY_ANIM_MAX_JOINTS) { dbglog("loadBodyAnimClip: %lu joints exceeds %d\n", (unsigned long)hdr.num_joints, BODY_ANIM_MAX_JOINTS); return false; }
	if (!BANIM_FITS(hdr.ofs_joints, hdr.num_joints, sizeof(BodyAnimIqmJoint))) { dbglog("loadBodyAnimClip: joints out of bounds\n"); return false; }
	out->jointCount = hdr.num_joints;
	for (u32 i = 0; i < hdr.num_joints; i++) {
		BodyAnimIqmJoint j;
		memcpy(&j, data + hdr.ofs_joints + (size_t)i * sizeof(j), sizeof(j));
		const char* name = (text && j.name < hdr.num_text) ? (text + j.name) : "";
		strncpy(out->jointNames[i], name, sizeof(out->jointNames[i]) - 1);
		out->jointNames[i][sizeof(out->jointNames[i]) - 1] = '\0';
	}

	if (hdr.num_poses == 0 || hdr.num_frames == 0) { dbglog("loadBodyAnimClip: no pose/frame data - this file has no animation to play\n"); return false; }
	if (hdr.num_poses != hdr.num_joints) { dbglog("loadBodyAnimClip: num_poses (%lu) != num_joints (%lu) - unsupported\n", (unsigned long)hdr.num_poses, (unsigned long)hdr.num_joints); return false; }
	if (!BANIM_FITS(hdr.ofs_poses, hdr.num_poses, sizeof(BodyAnimIqmPose))) { dbglog("loadBodyAnimClip: poses out of bounds\n"); return false; }
	BodyAnimIqmPose poses[BODY_ANIM_MAX_JOINTS];
	memcpy(poses, data + hdr.ofs_poses, (size_t)hdr.num_poses * sizeof(BodyAnimIqmPose));

	if (!BANIM_FITS(hdr.ofs_frames, (u64)hdr.num_frames * hdr.num_framechannels, sizeof(u16))) { dbglog("loadBodyAnimClip: frames out of bounds\n"); return false; }

	out->frameCount = hdr.num_frames;
	out->framerate = 30.0f;
	if (hdr.num_anims > 0 && BANIM_FITS(hdr.ofs_anims, 1, sizeof(BodyAnimIqmAnimHdr))) {
		BodyAnimIqmAnimHdr a;
		memcpy(&a, data + hdr.ofs_anims, sizeof(a));
		if (a.framerate > 0.0f) out->framerate = a.framerate;
	}

	out->framePoses = malloc(sizeof(BoneLocalPose) * (size_t)out->frameCount * out->jointCount);
	if (!out->framePoses) { dbglog("loadBodyAnimClip: out of memory for %lu frame(s)\n", (unsigned long)out->frameCount); memset(out, 0, sizeof(*out)); return false; }

	const u16* cursor = (const u16*)(data + hdr.ofs_frames);
	const u16* cursorEnd = cursor + (size_t)hdr.num_frames * hdr.num_framechannels;
	for (u32 f = 0; f < hdr.num_frames; f++) {
		for (u32 j = 0; j < hdr.num_joints; j++) {
			const BodyAnimIqmPose* pose = &poses[j];
			float vals[10];
			for (int c = 0; c < 10; c++) {
				if (pose->mask & (1u << c)) {
					if (cursor >= cursorEnd) { dbglog("loadBodyAnimClip: frame channel data overrun during decode\n"); free(out->framePoses); memset(out, 0, sizeof(*out)); return false; }
					vals[c] = pose->channeloffset[c] + (*cursor) * pose->channelscale[c];
					cursor++;
				} else {
					vals[c] = pose->channeloffset[c];
				}
			}
			BoneLocalPose* p = &out->framePoses[f * out->jointCount + j];
			p->translate[0] = vals[0]; p->translate[1] = vals[1]; p->translate[2] = vals[2];
			p->rotate[0] = vals[3]; p->rotate[1] = vals[4]; p->rotate[2] = vals[5]; p->rotate[3] = vals[6];
			bodyAnimQuatNormalize(p->rotate);
			p->scale[0] = vals[7]; p->scale[1] = vals[8]; p->scale[2] = vals[9];
		}
	}
	#undef BANIM_FITS
	dbglog("loadBodyAnimClip: parsed OK - %lu joint(s), %lu frame(s) @ %.1ffps\n",
		(unsigned long)out->jointCount, (unsigned long)out->frameCount, out->framerate);
	return true;
}

static void sampleBodyAnimFrame(const BodyAnimClip* clip, float frame, BoneLocalPose* outPoses)
{
	if (clip->frameCount == 0) return;
	float wrapped = fmodf(frame, (float)clip->frameCount);
	if (wrapped < 0.0f) wrapped += (float)clip->frameCount;
	u32 f0 = (u32)wrapped;
	u32 f1 = (f0 + 1) % clip->frameCount;
	float t = wrapped - (float)f0;

	for (u32 j = 0; j < clip->jointCount; j++) {
		const BoneLocalPose* a = &clip->framePoses[(size_t)f0 * clip->jointCount + j];
		const BoneLocalPose* b = &clip->framePoses[(size_t)f1 * clip->jointCount + j];
		BoneLocalPose* out = &outPoses[j];
		for (int c = 0; c < 3; c++) out->translate[c] = a->translate[c] + (b->translate[c] - a->translate[c]) * t;
		for (int c = 0; c < 3; c++) out->scale[c] = a->scale[c] + (b->scale[c] - a->scale[c]) * t;
		float dot = a->rotate[0]*b->rotate[0] + a->rotate[1]*b->rotate[1] + a->rotate[2]*b->rotate[2] + a->rotate[3]*b->rotate[3];
		float bs = dot < 0.0f ? -1.0f : 1.0f;
		for (int c = 0; c < 4; c++) out->rotate[c] = a->rotate[c] + (bs * b->rotate[c] - a->rotate[c]) * t;
		bodyAnimQuatNormalize(out->rotate);
	}
}

static BodyAnimClip bodyAnimClip;
static bool bodyAnimClipValid = false;

#define BODY_TEST_ANIMATION_DISABLED true
static s32 bodyAnimBoneToJoint[BODY_MAX_BONES];
static BoneLocalPose bodyAnimBindFallback[BODY_MAX_BONES];
static bool bodyAnimReady = false;
static bool bodyAnimPlaying = false;
static float bodyAnimFrame = 0.0f;

static CFLExpression bodyExpression = CFL_EXPRESSION_NORMAL;

static const char* kSampleBase64StoreData =
	"AwAEMAIHJxb3L3p/lBioWpzmNejD+wAANV5CMIQwSzAAAAAAAAAAAAAAAAAAAFEmAhBIAQpGZBoAMmUUgRQTZA4AACkAUkhQQjCEMEswAAAAAAAAAAAAAAAAAAAAANfO";

static CFLCharModel dataTestModel;
static bool dataTestModelValid = false;
static float dataTestSpin = 0.0f;
static char dataTestBase64Display[200];
static char dataTestMiiName[32];
static u32 dataTestMiiId;

static u32 s_lastMiiSelectorIndex = 0;
static bool s_lastMiiWasGuest = false;

static bool selectMii(MiiData* mii)
{
	memset(mii, 0, sizeof(*mii));

	MiiSelectorConf conf;
	MiiSelectorReturn ret;
	miiSelectorInit(&conf);
	miiSelectorSetTitle(&conf, "Select a Mii for the demo");
	
	miiSelectorSetOptions(&conf, MIISELECTOR_CANCEL | MIISELECTOR_GUESTS);
	miiSelectorSetInitialIndex(&conf, s_lastMiiSelectorIndex);
	conf.show_guest_page = s_lastMiiWasGuest ? 1 : 0;
	miiSelectorLaunch(&conf, &ret);

	if (ret.guest_mii_was_selected && ret.guest_mii_index != 0xFFFFFFFF) {
		s_lastMiiSelectorIndex = ret.guest_mii_index;
		s_lastMiiWasGuest = true;
	} else if (!ret.no_mii_selected) {
		u16 realIndex = 0;
		if (CFL_SearchOfficialData(&ret.mii, &realIndex)) {
			s_lastMiiSelectorIndex = realIndex;
			s_lastMiiWasGuest = false;
		} else {
			dbglogErr("selectMii: CFL_SearchOfficialData could not find this Mii's real database index\n");
		}
	}

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

static int base64DecodeChar(char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

static size_t base64Decode(const char* in, u8* out, size_t outCapacity)
{
	size_t outLen = 0;
	u32 buffer = 0;
	int bitsCollected = 0;
	for (const char* p = in; *p; p++) {
		char c = *p;
		if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
		int val = base64DecodeChar(c);
		if (val < 0) return 0;
		buffer = (buffer << 6) | (u32)val;
		bitsCollected += 6;
		if (bitsCollected >= 8) {
			bitsCollected -= 8;
			if (outLen >= outCapacity) return 0;
			out[outLen++] = (u8)((buffer >> bitsCollected) & 0xFF);
		}
	}
	return outLen;
}

static bool miiFromBase64StoreData(const char* base64, MiiData* outMii)
{
	u8 raw[sizeof(CFLStoreData)];
	size_t n = base64Decode(base64, raw, sizeof(raw));
	if (n != sizeof(CFLStoreData)) {
		dbglogErr("miiFromBase64StoreData: decoded %u bytes, expected %u\n",
			(unsigned)n, (unsigned)sizeof(CFLStoreData));
		return false;
	}
	const CFLStoreData* store = (const CFLStoreData*)raw;
	if (!CFL_IsStoreDataValid(store)) {
		dbglogErr("miiFromBase64StoreData: checksum invalid\n");
		return false;
	}
	*outMii = store->miiData;
	return true;
}

static void base64Encode(const u8* data, size_t len, char* out, size_t outCapacity)
{
	static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t o = 0;
	size_t i = 0;
	for (; i + 3 <= len && o + 4 < outCapacity; i += 3) {
		u32 n = ((u32)data[i] << 16) | ((u32)data[i + 1] << 8) | data[i + 2];
		out[o++] = kAlphabet[(n >> 18) & 0x3F];
		out[o++] = kAlphabet[(n >> 12) & 0x3F];
		out[o++] = kAlphabet[(n >> 6) & 0x3F];
		out[o++] = kAlphabet[n & 0x3F];
	}
	size_t rem = len - i;
	if (rem == 1 && o + 4 < outCapacity) {
		u32 n = (u32)data[i] << 16;
		out[o++] = kAlphabet[(n >> 18) & 0x3F];
		out[o++] = kAlphabet[(n >> 12) & 0x3F];
		out[o++] = '=';
		out[o++] = '=';
	} else if (rem == 2 && o + 4 < outCapacity) {
		u32 n = ((u32)data[i] << 16) | ((u32)data[i + 1] << 8);
		out[o++] = kAlphabet[(n >> 18) & 0x3F];
		out[o++] = kAlphabet[(n >> 12) & 0x3F];
		out[o++] = kAlphabet[(n >> 6) & 0x3F];
		out[o++] = '=';
	}
	out[o] = '\0';
}

static void wrapTextForDisplay(const char* in, char* out, size_t outCapacity, int wrapWidth)
{
	size_t o = 0;
	int col = 0;
	for (const char* p = in; *p && o + 2 < outCapacity; p++) {
		out[o++] = *p;
		if (++col >= wrapWidth) {
			out[o++] = '\n';
			col = 0;
		}
	}
	out[o] = '\0';
}

static CFLExpression nextTestExpression(CFLExpression from)
{
	for (int step = 1; step <= CFL_EXPRESSION_COUNT; step++) {
		CFLExpression candidate = (CFLExpression)(((int)from + step) % CFL_EXPRESSION_COUNT);
		if (testExpressionFlags & CFL_EXPRESSION_FLAG(candidate)) return candidate;
	}
	return from;
}

static void getSlotOffset(int index, int count, float* outX, float* outY)
{
	static const float SPACING = 3.3f;
	if (count <= 1) { *outX = 0.0f; *outY = 0.0f; return; }
	*outX = ((float)index - (float)(count - 1) * 0.5f) * SPACING;
	*outY = 0.0f;
}

static void sceneBuildCameraModelView(C3D_Mtx* out, float slotX, float slotY, float spinYaw)
{
	Mtx_Identity(out);
	Mtx_Translate(out, headX + slotX, headY + slotY, -cameraDist, true);
	Mtx_RotateX(out, pitch, true);
	Mtx_RotateY(out, yaw, true);
	Mtx_RotateY(out, spinYaw, true);
}

static void sceneDrawCharModelParts(const CFLCharModel* cm, const C3D_Mtx* modelView)
{
	CFLShaderLocations loc = CFL_GetShaderLocations();
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.projection, &projection);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.modelView,  modelView);

	CFL_BindDefaultShader();

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

			CFL_SetDefaultMaterial(part->color, part->noSpecular);

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

			if (part->capBlend) {
				C3D_TexEnv* env2 = C3D_GetTexEnv(2);
				C3D_TexEnvInit(env2);
				C3D_TexEnvSrc(env2, C3D_RGB, GPU_PREVIOUS, GPU_FRAGMENT_PRIMARY_COLOR, 0);
				C3D_TexEnvFunc(env2, C3D_RGB, GPU_ADD);
				C3D_TexEnvSrc(env2, C3D_Alpha, GPU_PREVIOUS, 0, 0);
				C3D_TexEnvFunc(env2, C3D_Alpha, GPU_REPLACE);
				C3D_DirtyTexEnv(env2);
			}

			if (part->useIndices)
				C3D_DrawElements(GPU_TRIANGLES, part->indexCount, C3D_UNSIGNED_BYTE, part->ibo);
			else
				C3D_DrawArrays(GPU_TRIANGLES, 0, part->vertexCount);

			if (part->capBlend) {
				C3D_TexEnv* env2 = C3D_GetTexEnv(2);
				C3D_TexEnvInit(env2);
				C3D_DirtyTexEnv(env2);
			}
		}
	}
}

static void sceneRenderModel(const CFLCharModel* cm, float slotX, float slotY, float spinYaw)
{
	C3D_Mtx modelView;
	sceneBuildCameraModelView(&modelView, slotX, slotY, spinYaw);
	Mtx_Scale(&modelView, scale, scale, scale);
	sceneDrawCharModelParts(cm, &modelView);
}

static void sceneRenderBody(const BodyModel* body, const C3D_Mtx* modelView)
{
	CFLShaderLocations loc = CFL_GetShaderLocations();
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.projection, &projection);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.modelView, modelView);
	CFL_BindDefaultShader();

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

	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);

	for (int i = 0; i < body->partCount; i++) {
		const BodyPart* part = &body->parts[i];
		C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);

		C3D_BufInfo* bufInfo = C3D_GetBufInfo();
		BufInfo_Init(bufInfo);
		BufInfo_Add(bufInfo, part->vbo, sizeof(Vertex), 3, 0x210);

		CFL_SetDefaultMaterial(part->color, false);

		C3D_TexEnv* env = C3D_GetTexEnv(0);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_Both, GPU_FRAGMENT_PRIMARY_COLOR, 0, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

		C3D_DrawElements(GPU_TRIANGLES, part->indexCount, C3D_UNSIGNED_BYTE, part->ibo);
	}
}

#define HEAD_TO_BODY_SCALE (10.0f / 7.0f)

static void sceneRenderModelWithBody(const CFLCharModel* cm, const BodyModel* body, float slotX, float slotY, float spinYaw)
{
	C3D_Mtx cameraView;
	sceneBuildCameraModelView(&cameraView, slotX, slotY, spinYaw);

	if (body && body->partCount > 0) {
		C3D_Mtx bodyView = cameraView;
		Mtx_Scale(&bodyView, scale, scale, scale);
		sceneRenderBody(body, &bodyView);
	}

	C3D_Mtx headView = cameraView;
	if (body && body->hasHeadBone) {
		const float* m = body->headBoneWorldMatrix;
		float headScale = scale * HEAD_TO_BODY_SCALE * body->headWorldScale;
		Mtx_Translate(&headView, m[3] * scale, m[7] * scale, m[11] * scale, true);
		Mtx_Scale(&headView, headScale, headScale, headScale);
	} else {
		Mtx_Scale(&headView, scale, scale, scale);
	}
	sceneDrawCharModelParts(cm, &headView);
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
	
	snprintf(label, sizeof(label), "Expression: %s\n(SELECT: replace last Mii)",
		CFL_GetExpressionName(currentExpression));

	C2D_Prepare();
	C2D_SceneBegin(target);
	C2D_TextBufClear(s_textBuf);
	C2D_Text text;
	C2D_TextParse(&text, s_textBuf, label);
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor, 10.0f, 10.0f, 0.0f, 0.5f, 0.5f, C2D_Color32(255, 255, 255, 255));
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
	for (int i = 0; i < modelCount; i++) CFL_DeleteModel(&models[i]);
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
		dbglogVramStats("startCharModelTest before CFL_InitCharModel", false);
		
		modelCount = i + 1;
		if (!CFL_InitCharModel(&models[i], &mii, cfg->resolution, cfg->expressionFlags)) {
			dbglogErr("\nCould not build CharModel %d/%d for this test.\n", i + 1, cfg->modelCount);
			dbglogVramStats("startCharModelTest CFL_InitCharModel failure", true);
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

static void sanitizeMiiName16(u16* name, int maxChars)
{
	for (int i = 0; i < maxChars; i++) {
		if (name[i] == 0) break;
		if (name[i] < 0x20 || name[i] == 0x7f) name[i] = u'?';
	}
}

static bool miiCharsetMatchesConsoleRegion(u8 charSet, u8 region)
{
	switch (charSet) {
		case 0: return region == CFG_REGION_JPN || region == CFG_REGION_USA ||
		               region == CFG_REGION_EUR || region == CFG_REGION_AUS;
		case 1: return region == CFG_REGION_CHN;
		case 2: return region == CFG_REGION_KOR;
		case 3: return region == CFG_REGION_TWN;
		default: return false;
	}
}

static bool getHomeAuthorId(u64* outAuthorId)
{
	return R_SUCCEEDED(CFGU_GenHashConsoleUnique(0, outAuthorId));
}

static void getSafeMiiName16(const MiiData* mii, u16 outName[11])
{
	if (mii->mii_options.is_private_name) {
		u64 myAuthorId;
		bool isMine = getHomeAuthorId(&myAuthorId) && myAuthorId == mii->system_id;
		if (!isMine) {
			outName[0] = u'?'; outName[1] = u'?'; outName[2] = u'?'; outName[3] = 0;
			return;
		}
		
	}

	memcpy(outName, mii->mii_name, sizeof(mii->mii_name));
	outName[10] = 0;
	sanitizeMiiName16(outName, 10);

	u8 region;
	if (R_SUCCEEDED(CFGU_SecureInfoGetRegion(&region)) &&
		!miiCharsetMatchesConsoleRegion(mii->mii_options.char_set, region)) {
		for (int i = 0; i < 10; i++) {
			if (outName[i] >= 0x80) outName[i] = u'?';
		}
	}
}

static void utf16ToUtf8(const u16* utf16, int maxChars, char* out, size_t outCapacity)
{
	size_t o = 0;
	for (int i = 0; i < maxChars && utf16[i] != 0 && o + 3 < outCapacity; i++) {
		u32 cp = utf16[i];
		if (cp < 0x80) {
			out[o++] = (char)cp;
		} else if (cp < 0x800) {
			out[o++] = (char)(0xC0 | (cp >> 6));
			out[o++] = (char)(0x80 | (cp & 0x3F));
		} else {
			out[o++] = (char)(0xE0 | (cp >> 12));
			out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			out[o++] = (char)(0x80 | (cp & 0x3F));
		}
	}
	out[o] = '\0';
}

static void updateDataTestDisplay(const MiiData* mii)
{
	
	u16 nameBuf[11];
	getSafeMiiName16(mii, nameBuf);
	utf16ToUtf8(nameBuf, 10, dataTestMiiName, sizeof(dataTestMiiName));
	dataTestMiiId = mii->mii_id;

	CFLStoreData store;
	if (CFL_MakeStoreData(mii, &store)) {
		char raw[200];
		base64Encode((const u8*)&store, sizeof(store), raw, sizeof(raw));
		wrapTextForDisplay(raw, dataTestBase64Display, sizeof(dataTestBase64Display), 32);
	} else {
		snprintf(dataTestBase64Display, sizeof(dataTestBase64Display), "(failed to encode)");
	}
}

static bool rebuildDataTestModel(const MiiData* mii)
{
	CFL_DeleteModel(&dataTestModel);
	if (!CFL_InitCharModel(&dataTestModel, mii, CFL_RESOLUTION_256, CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL))) {
		dataTestModelValid = false;
		return false;
	}
	dataTestModelValid = true;
	updateDataTestDisplay(mii);
	return true;
}

static void startDataTest(void)
{
	CFL_DeleteModel(&dataTestModel);
	dataTestModelValid = false;

	MiiData mii;
	if (!miiFromBase64StoreData(kSampleBase64StoreData, &mii)) {
		dbglogErr("\nCharModel from Data: could not decode the sample base64 StoreData.\n");
		screen = SCREEN_TITLE;
		return;
	}
	if (!rebuildDataTestModel(&mii)) {
		dbglogErr("\nCharModel from Data: could not build a CharModel from the decoded Mii.\n");
		screen = SCREEN_TITLE;
		return;
	}
	dataTestSpin = 0.0f;

	cameraDist = 3.2f;
	yaw = 0.0f; pitch = 0.0f; headX = 0.0f; headY = 0.0f;
	screen = SCREEN_DATA_TEST;
}

static void drawDataTest(C3D_RenderTarget* target)
{
	CFL_RebindShader();
	if (dataTestModelValid)
		sceneRenderModel(&dataTestModel, 0.0f, 0.0f, dataTestSpin);

	char label[400];
	snprintf(label, sizeof(label), "CharModel from Data   (SELECT: pick a Mii   B: back)\nName: %s   ID: %08lX\n%s",
		dataTestMiiName, (unsigned long)dataTestMiiId, dataTestBase64Display);

	C2D_Prepare();
	C2D_SceneBegin(target);
	C2D_TextBufClear(s_textBuf);
	C2D_Text text;
	C2D_TextParse(&text, s_textBuf, label);
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor, 10.0f, 10.0f, 0.0f, 0.45f, 0.45f, C2D_Color32(255, 255, 255, 255));
}

static void appIconDrawBodyParts(const BodyModel* body, const C3D_Mtx* iconProjection, const C3D_Mtx* modelView)
{
	CFLShaderLocations loc = CFL_GetShaderLocations();
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.projection, iconProjection);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.modelView, modelView);

	for (int i = 0; i < body->partCount; i++) {
		const BodyPart* part = &body->parts[i];
		C3D_DepthTest(true, GPU_LEQUAL, GPU_WRITE_ALL);

		C3D_BufInfo* bufInfo = C3D_GetBufInfo();
		BufInfo_Init(bufInfo);
		BufInfo_Add(bufInfo, part->vbo, sizeof(Vertex), 3, 0x210);

		CFL_SetDefaultMaterial(part->color, false);

		C3D_TexEnv* env = C3D_GetTexEnv(0);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_Both, GPU_FRAGMENT_PRIMARY_COLOR, 0, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

		C3D_DrawElements(GPU_TRIANGLES, part->indexCount, C3D_UNSIGNED_BYTE, part->ibo);
	}
}

static void appIconDrawCharModelParts(CFLCharModel* cm, CFLExpression expression, const CFLIconSetting* setting, const C3D_Mtx* iconProjection, const C3D_Mtx* modelView)
{
	CFLShaderLocations loc = CFL_GetShaderLocations();
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.projection, iconProjection);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc.modelView, modelView);

	C3D_Tex* iconMaskTex = NULL;
	if (expression >= 0 && expression < CFL_EXPRESSION_COUNT && cm->maskTexBaked[expression])
		iconMaskTex = &cm->maskTexForExpr[expression];

	CFLIconCustomCallback customCallback = setting ? setting->customCallback : NULL;
	void* customArgument = setting ? setting->customArgument : NULL;

	for (int pass = 0; pass < 2; pass++) {
		bool texturedPass = (pass == 1);
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
		for (int i = 0; i < cm->partCount; i++) {
			const CFLPart* part = &cm->parts[i];
			if (part->hasTexture != texturedPass) continue;

			C3D_Tex* texToUse = (iconMaskTex && i == cm->maskPartIndex) ? iconMaskTex : (C3D_Tex*)&part->tex;

			C3D_DepthTest(true, GPU_LEQUAL, part->depthWrite ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
			C3D_BufInfo* bufInfo = C3D_GetBufInfo();
			BufInfo_Init(bufInfo);
			BufInfo_Add(bufInfo, part->vbo, sizeof(Vertex), 3, 0x210);

			if (part->hasTexture) C3D_TexBind(0, texToUse);

			if (customCallback) {
				customCallback(customArgument, part, iconProjection, modelView);
			} else {
				CFL_SetDefaultMaterial(part->color, part->noSpecular);

				C3D_TexEnv* env = C3D_GetTexEnv(0);
				if (part->hasTexture) {
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

				if (part->capBlend) {
					C3D_TexEnv* env2 = C3D_GetTexEnv(2);
					C3D_TexEnvInit(env2);
					C3D_TexEnvSrc(env2, C3D_RGB, GPU_PREVIOUS, GPU_FRAGMENT_PRIMARY_COLOR, 0);
					C3D_TexEnvFunc(env2, C3D_RGB, GPU_ADD);
					C3D_TexEnvSrc(env2, C3D_Alpha, GPU_PREVIOUS, 0, 0);
					C3D_TexEnvFunc(env2, C3D_Alpha, GPU_REPLACE);
					C3D_DirtyTexEnv(env2);
				}
			}

			if (part->useIndices)
				C3D_DrawElements(GPU_TRIANGLES, part->indexCount, C3D_UNSIGNED_BYTE, part->ibo);
			else
				C3D_DrawArrays(GPU_TRIANGLES, 0, part->vertexCount);

			if (part->capBlend) {
				C3D_TexEnv* env2 = C3D_GetTexEnv(2);
				C3D_TexEnvInit(env2);
				C3D_DirtyTexEnv(env2);
			}
		}
	}
}

static bool appMakeModelIconWithBody(CFLCharModel* cm, CFLExpression expression, int iconSize, const CFLIconSetting* setting, C3D_Tex* outIcon)
{
	dbglog("appMakeModelIconWithBody: start iconSize=%d\n", iconSize);
	if (!cm || !CFL_HasCharModel(cm) || iconSize <= 0 || !outIcon) return false;

	BodyModel iconBody;
	bool haveIconBody = loadBodyModel(IconBody_iqm, IconBody_iqm_size, &cm->mii, true, &iconBody);
	if (!haveIconBody) dbglog("appMakeModelIconWithBody: loadBodyModel failed - rendering head only\n");

	if (outIcon->data) C3D_TexDelete(outIcon);
	if (!C3D_TexInitVRAM(outIcon, iconSize, iconSize, GPU_RGBA8)) {
		dbglog("appMakeModelIconWithBody: C3D_TexInitVRAM failed (%dx%d)\n", iconSize, iconSize);
		if (haveIconBody) deleteBodyModel(&iconBody);
		return false;
	}
	C3D_RenderTarget* iconTarget = C3D_RenderTargetCreateFromTex(outIcon, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24_STENCIL8);
	if (!iconTarget) {
		dbglog("appMakeModelIconWithBody: C3D_RenderTargetCreateFromTex failed (%dx%d)\n", iconSize, iconSize);
		C3D_TexDelete(outIcon);
		if (haveIconBody) deleteBodyModel(&iconBody);
		return false;
	}

	C3D_Mtx iconProjection, iconModelView;
	Mtx_Persp(&iconProjection, C3D_AngleFromDegrees(9.8762f), 1.0f, 500.0f, 1000.0f, false);
	iconProjection.r[1].y = -iconProjection.r[1].y;
	iconProjection.r[2].x = 0.0f;
	iconProjection.r[2].y = 0.0f;
	iconProjection.r[2].z = 3.5f;
	iconProjection.r[2].w = 1750.0f;
	iconProjection.r[3].x = 0.0f;
	iconProjection.r[3].y = 0.0f;
	iconProjection.r[3].z = -1.0f;
	iconProjection.r[3].w = 0.0f;

	Mtx_Identity(&iconModelView);
	Mtx_Translate(&iconModelView, 0.0f, -34.5f, -600.0f, true);

	CFLIconBGType bgType = setting ? setting->bgType : CFL_ICON_BG_FAVORITE;
	u32 clearColor = 0x00000000;
	if (bgType == CFL_ICON_BG_DIRECT && setting) {
		clearColor =
			((u32)(setting->bgColor[0] * 255.0f) << 24) |
			((u32)(setting->bgColor[1] * 255.0f) << 16) |
			((u32)(setting->bgColor[2] * 255.0f) << 8)  |
			(u32)(setting->bgColor[3] * 255.0f);
	} else if (bgType == CFL_ICON_BG_FAVORITE) {
		const float* fc = CFL_GetFavoriteColor(cm->mii.mii_details.shirt_color);
		clearColor = ((u32)(fc[0] * 255.0f) << 24) | ((u32)(fc[1] * 255.0f) << 16) | ((u32)(fc[2] * 255.0f) << 8) | 0xFF;
	}

	bool hasCustomCallback = setting && setting->customCallback;

	dbglog("appMakeModelIconWithBody: about to C3D_FrameBegin(SYNCDRAW)\n");
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		dbglog("appMakeModelIconWithBody: C3D_FrameBegin returned\n");
		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_INVALIDATE, 1);
		if (bgType != CFL_ICON_BG_NO_CLEAR)
			C3D_RenderTargetClear(iconTarget, C3D_CLEAR_ALL, clearColor, 0xFFFFFF00);
		C3D_FrameDrawOn(iconTarget);
		C3D_SetViewport(0, 0, iconSize, iconSize);
		if (!hasCustomCallback) CFL_BindDefaultShader();
		C3D_CullFace(GPU_CULL_NONE);

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

		if (haveIconBody && iconBody.partCount > 0) {
			C3D_Mtx bodyIconView = iconModelView;
			if (iconBody.hasHeadBone) {
				const float* m = iconBody.headBoneWorldMatrix;
				Mtx_Translate(&bodyIconView, -m[3], -m[7], -m[11], true);
			}
			appIconDrawBodyParts(&iconBody, &iconProjection, &bodyIconView);
		}

		C3D_Mtx headModelView = iconModelView;
		appIconDrawCharModelParts(cm, expression, setting, &iconProjection, &headModelView);

		GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH, 1);
	C3D_FrameEnd(0);

	C3D_FrameSync();
	C3D_RenderTargetDelete(iconTarget);
	C3D_TexSetFilter(outIcon, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(outIcon, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	if (haveIconBody) deleteBodyModel(&iconBody);
	dbglog("appMakeModelIconWithBody: done\n");
	return true;
}

static bool renderIconPair(bool withBody, C3D_Tex* out256, bool* out256Valid, C3D_Tex* out128, bool* out128Valid)
{
	*out256Valid = false;
	*out128Valid = false;

	CFLIconSetting transparentSetting = { CFL_ICON_BG_DIRECT, { 0.0f, 0.0f, 0.0f, 0.0f }, NULL, NULL };
	bool ok256 = withBody
		? appMakeModelIconWithBody(&iconModel, CFL_EXPRESSION_NORMAL, 256, &transparentSetting, out256)
		: CFL_CommandMakeModelIcon(&iconModel, CFL_EXPRESSION_NORMAL, 256, &transparentSetting, out256);
	if (!ok256) {
		dbglogErr("\nIcon Test: make-icon (256, withBody=%d) failed.\n", withBody);
		return false;
	}
	*out256Valid = true;
	bool ok128 = withBody
		? appMakeModelIconWithBody(&iconModel, CFL_EXPRESSION_NORMAL, 128, NULL, out128)
		: CFL_CommandMakeModelIcon(&iconModel, CFL_EXPRESSION_NORMAL, 128, NULL, out128);
	if (!ok128) {
		dbglogErr("\nIcon Test: make-icon (128, withBody=%d) failed.\n", withBody);
		return false;
	}
	*out128Valid = true;
	return true;
}

static void startIconTest(void)
{
	CFL_DeleteModel(&iconModel);
	
	releaseIconTextures();
	iconShowBody = false;

	MiiData mii;
	selectMii(&mii);
	if (!CFL_InitCharModel(&iconModel, &mii, CFL_RESOLUTION_256, CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL))) {
		dbglogErr("\nIcon Test: could not build a CharModel from this Mii.\n");
		screen = SCREEN_TITLE;
		return;
	}

	if (!renderIconPair(false, &iconTexture256NoBody, &iconTexture256NoBodyValid, &iconTexture128NoBody, &iconTexture128NoBodyValid)) {
		CFL_DeleteModel(&iconModel);
		releaseIconTextures();
		screen = SCREEN_TITLE;
		return;
	}
	if (!renderIconPair(true, &iconTexture256Body, &iconTexture256BodyValid, &iconTexture128Body, &iconTexture128BodyValid)) {
		dbglogErr("\nIcon Test: body-enabled icon pair failed to render - X toggle unavailable this session.\n");
	}
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
	CFL_BindDefaultShader();

	{
		C3D_TexEnv* env1 = C3D_GetTexEnv(1);
		C3D_TexEnvInit(env1);
		C3D_DirtyTexEnv(env1);
		C3D_TexEnv* env2 = C3D_GetTexEnv(2);
		C3D_TexEnvInit(env2);
		C3D_DirtyTexEnv(env2);
	}

	static const float white[3] = { 1.0f, 1.0f, 1.0f };
	CFL_SetDefaultMaterial(white, true);

	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);

	bool bodyPairReady = iconTexture256BodyValid && iconTexture128BodyValid;
	bool showingBody = iconShowBody && bodyPairReady;
	C3D_Tex* tex256 = showingBody ? &iconTexture256Body : &iconTexture256NoBody;
	C3D_Tex* tex128 = showingBody ? &iconTexture128Body : &iconTexture128NoBody;
	bool tex256Valid = showingBody ? iconTexture256BodyValid : iconTexture256NoBodyValid;
	bool tex128Valid = showingBody ? iconTexture128BodyValid : iconTexture128NoBodyValid;

	static const float kBottomY = -0.9f;
	float half256 = 0.7f, half128 = 0.35f;
	if (tex256Valid)
		drawIconQuad(0, tex256, -0.75f, kBottomY + half256, half256);
	if (tex128Valid)
		drawIconQuad(1, tex128, 0.65f, kBottomY + half128, half128);

	char label[160];
	const char* iconFnName = showingBody ? "appMakeModelIconWithBody()" : "CFL_CommandMakeModelIcon()";
	if (bodyPairReady) {
		snprintf(label, sizeof(label), "Icon Test - %s 256px + 128px\n(X: body %s   B: back)",
			iconFnName, iconShowBody ? "ON" : "OFF");
	} else {
		snprintf(label, sizeof(label), "Icon Test - %s 256px + 128px\n(body failed to load - B: back)", iconFnName);
	}

	C2D_Prepare();
	C2D_SceneBegin(target);
	C2D_TextBufClear(s_textBuf);
	C2D_Text text;
	C2D_TextParse(&text, s_textBuf, label);
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor, 10.0f, 10.0f, 0.0f, 0.5f, 0.5f, C2D_Color32(255, 255, 255, 255));
}

static void loadBodyTestBody(void)
{
	if (bodyTestBodyValid) { deleteBodyModel(&bodyTestBody); bodyTestBodyValid = false; }

	const u8* bodyData;
	u32 bodySize;
	bool applyMiiScale;
	if (bodyTestSource == BODY_SOURCE_STREETPASS) {
		bodyData = bodyTestMii.mii_details.sex ? StreetPassBodyFemale_iqm : StreetPassBodyMale_iqm;
		bodySize = bodyTestMii.mii_details.sex ? StreetPassBodyFemale_iqm_size : StreetPassBodyMale_iqm_size;
		applyMiiScale = false;
	} else {
		bodyData = bodyTestMii.mii_details.sex ? FemaleBody_iqm : MaleBody_iqm;
		bodySize = bodyTestMii.mii_details.sex ? FemaleBody_iqm_size : MaleBody_iqm_size;
		applyMiiScale = true;
	}
	if (!loadBodyModel(bodyData, bodySize, &bodyTestMii, applyMiiScale, &bodyTestBody)) {
		dbglogErr("\nBody Test: could not load a body model for this Mii (showing head only).\n");
		bodyTestBodyValid = false;
	} else {
		bodyTestBodyValid = true;
	}

	bodyAnimReady = false;
	if (bodyTestBodyValid && bodyAnimClipValid) {
		u32 boneCount = getBodyBoneCount(&bodyTestBody);
		u32 matched = 0;
		for (u32 i = 0; i < boneCount && i < BODY_MAX_BONES; i++) {
			const char* boneName = getBodyBoneName(&bodyTestBody, i);
			bodyAnimBoneToJoint[i] = -1;
			if (boneName) {
				for (u32 j = 0; j < bodyAnimClip.jointCount; j++) {
					if (strcmp(boneName, bodyAnimClip.jointNames[j]) == 0) { bodyAnimBoneToJoint[i] = (s32)j; matched++; break; }
				}
			}
			getBodyBoneBindLocalPose(&bodyTestBody, i, &bodyAnimBindFallback[i]);
		}
		dbglog("Body Test: animation clip matched %lu/%lu bones by name\n", (unsigned long)matched, (unsigned long)boneCount);
		bodyAnimReady = (matched > 0) && !BODY_TEST_ANIMATION_DISABLED;
	}
	
	bodyAnimPlaying = bodyAnimReady;
	bodyAnimFrame = 0.0f;
	bodyExpression = CFL_EXPRESSION_NORMAL;
}

static void startBodyTest(void)
{
	CFL_DeleteModel(&bodyTestModel);
	bodyTestModelValid = false;
	if (bodyTestBodyValid) { deleteBodyModel(&bodyTestBody); bodyTestBodyValid = false; }

	selectMii(&bodyTestMii);
	
	if (!CFL_InitCharModel(&bodyTestModel, &bodyTestMii, CFL_RESOLUTION_256,
			CFL_EXPRESSION_FLAG(CFL_EXPRESSION_NORMAL) | CFL_EXPRESSION_FLAG(CFL_EXPRESSION_SURPRISE))) {
		dbglogErr("\nBody Test: could not build a CharModel from this Mii.\n");
		screen = SCREEN_TITLE;
		return;
	}
	bodyTestModelValid = true;

	bodyTestSource = BODY_SOURCE_MII_MAKER;
	loadBodyTestBody();

	bodySpin = 0.0f;
	
	cameraDist = 20.0f;
	yaw = 0.0f;
	pitch = 0.0f;
	headX = 0.0f;
	headY = 0.5f;
	screen = SCREEN_BODY_TEST;
}

static void drawBodyTest(C3D_RenderTarget* target)
{
	CFL_RebindShader();
	if (bodyTestModelValid) {
		sceneRenderModelWithBody(&bodyTestModel, bodyTestBodyValid ? &bodyTestBody : NULL, 0.0f, 0.0f, bodySpin);
	}

	const char* sourceName = bodyTestSource == BODY_SOURCE_STREETPASS ? "StreetPass (fixed)" : "Mii Maker (scaled)";
	char label[220];
	if (bodyTestBodyValid && bodyAnimReady) {
		snprintf(label, sizeof(label), "CharModel + Body Test - %s\n(SELECT: pick a Mii   X: anim %s   Y: swap body   B: back)",
			sourceName, bodyAnimPlaying ? "ON" : "OFF");
	} else if (bodyTestBodyValid) {
		snprintf(label, sizeof(label), "CharModel + Body Test - %s\n(SELECT: pick a Mii   Y: swap body   B: back)", sourceName);
	} else {
		snprintf(label, sizeof(label), "CharModel + Body Test - body failed to load, head only\n(SELECT: pick a Mii   Y: swap body   B: back)");
	}

	C2D_Prepare();
	C2D_SceneBegin(target);
	C2D_TextBufClear(s_textBuf);
	C2D_Text text;
	C2D_TextParse(&text, s_textBuf, label);
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor, 10.0f, 10.0f, 0.0f, 0.5f, 0.5f, C2D_Color32(255, 255, 255, 255));
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
	
	cfguInit();

	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C3D_RenderTarget* target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
	if (!CFL_Initialize()) {
		dbglogErr("\nCFL_Initialize failed - see the log above for specifics\n");
		dbglog("(most likely the archive read needing full ARM11 FS\n");
		dbglog("permissions - launch via Luma3DS/Rosalina's homebrew\n");
		dbglog("launcher).\n");
		C3D_Fini();
		waitForStartAndExit();
		CFL_EnableSDDebug(false);
		cfguExit();
		gfxExit();
		return 0;
	}
	Mtx_PerspTilt(&projection, C3D_AngleFromDegrees(50.0f), C3D_AspectRatioTop, 0.01f, 1000.0f, false);

	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	s_textBuf = C2D_TextBufNew(256);

	bodyAnimClipValid = loadBodyAnimClip(DefaultAnim_iqm, DefaultAnim_iqm_size, &bodyAnimClip);
	if (!bodyAnimClipValid) dbglog("Default animation clip failed to load - Body Test's animation toggle will be unavailable.\n");

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
				} else if (titleSelection == 1) {
					startBodyTest();
				} else if (titleSelection == 2) {
					startIconTest();
				} else {
					startDataTest();
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
					CFL_DeleteModel(&models[slot]);
					if (CFL_InitCharModel(&models[slot], &mii, activeResolution, activeExpressionFlags)) {
						CFL_SetExpression(&models[slot], currentExpression);
						
					} else {
						dbglogErr("\nCould not rebuild CharModel %d after reselect.\n", slot);
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
				CFL_DeleteModel(&iconModel);
				releaseIconTextures();
				screen = SCREEN_TITLE;
				break;
			}
			
			if ((kDown & KEY_X) && iconTexture256BodyValid && iconTexture128BodyValid) {
				iconShowBody = !iconShowBody;
				dbglog("Icon Test: X pressed, iconShowBody now %d (pre-rendered, no re-render)\n", iconShowBody);
			}
			break;

		case SCREEN_BODY_TEST:
			if (kDown & KEY_B) {
				CFL_DeleteModel(&bodyTestModel);
				bodyTestModelValid = false;
				if (bodyTestBodyValid) { deleteBodyModel(&bodyTestBody); bodyTestBodyValid = false; }
				screen = SCREEN_TITLE;
				break;
			}
			if (kDown & KEY_SELECT) {
				startBodyTest();
				break;
			}
			
			if ((kDown & KEY_X) && bodyAnimReady) {
				bodyAnimPlaying = !bodyAnimPlaying;
			}
			
			if (kDown & KEY_Y) {
				bodyTestSource = (bodyTestSource == BODY_SOURCE_MII_MAKER) ? BODY_SOURCE_STREETPASS : BODY_SOURCE_MII_MAKER;
				loadBodyTestBody();
				dbglog("Body Test: Y pressed, bodyTestSource now %s\n",
					bodyTestSource == BODY_SOURCE_STREETPASS ? "StreetPass (fixed)" : "Mii Maker (scaled)");
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

			if (bodyAnimPlaying && bodyAnimReady) {
				bodyAnimFrame += bodyAnimClip.framerate / 60.0f;

				float wrappedFrame = fmodf(bodyAnimFrame, (float)bodyAnimClip.frameCount);
				if (wrappedFrame < 0.0f) wrappedFrame += (float)bodyAnimClip.frameCount;
				u32 curFrame = (u32)wrappedFrame;
				CFLExpression wantExpr = (curFrame >= 4 && curFrame <= 10) ? CFL_EXPRESSION_SURPRISE : CFL_EXPRESSION_NORMAL;
				if (bodyTestModelValid && wantExpr != bodyExpression) {
					CFL_SetExpression(&bodyTestModel, wantExpr);
					bodyExpression = wantExpr;
				}

				BoneLocalPose sampledByJoint[BODY_ANIM_MAX_JOINTS];
				sampleBodyAnimFrame(&bodyAnimClip, bodyAnimFrame, sampledByJoint);
				BoneLocalPose poseByBone[BODY_MAX_BONES];
				u32 boneCount = getBodyBoneCount(&bodyTestBody);
				for (u32 i = 0; i < boneCount && i < BODY_MAX_BONES; i++) {
					poseByBone[i] = (bodyAnimBoneToJoint[i] >= 0) ? sampledByJoint[bodyAnimBoneToJoint[i]] : bodyAnimBindFallback[i];
				}
				poseBodyModel(&bodyTestBody, poseByBone, boneCount);
			}

			bodySpin += 0.015f;
			break;

		case SCREEN_DATA_TEST:
			if (kDown & KEY_B) {
				CFL_DeleteModel(&dataTestModel);
				dataTestModelValid = false;
				screen = SCREEN_TITLE;
				break;
			}
			
			if (kDown & KEY_SELECT) {
				MiiData mii;
				if (selectMii(&mii)) {
					if (!rebuildDataTestModel(&mii))
						dbglogErr("\nCharModel from Data: could not build a CharModel from the selected Mii.\n");
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

			dataTestSpin += 0.015f;
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
			case SCREEN_BODY_TEST:
				drawBodyTest(target);
				break;
			case SCREEN_DATA_TEST:
				drawDataTest(target);
				break;
			}
		C3D_FrameEnd(0);
	}

	destroyTestModels();
	CFL_DeleteModel(&iconModel);
	releaseIconTextures();
	CFL_DeleteModel(&bodyTestModel);
	if (bodyTestBodyValid) deleteBodyModel(&bodyTestBody);
	CFL_DeleteModel(&dataTestModel);
	for (int i = 0; i < ICON_QUAD_SLOTS; i++) {
		if (s_iconQuadVBO[i]) linearFree(s_iconQuadVBO[i]);
		if (s_iconQuadIBO[i]) linearFree(s_iconQuadIBO[i]);
	}
	CFL_Finalize();

	C2D_TextBufDelete(s_textBuf);
	C2D_Fini();
	C3D_Fini();
	CFL_EnableSDDebug(false);
	cfguExit();
	gfxExit();
	return 0;
}
