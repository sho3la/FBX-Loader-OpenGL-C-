#pragma once
#include <GL\glew.h>
#include "lib\include\fbxsdk.h"
#include "targa.h"


class FBXLoader
{
public:

	///////////////////////////////////////////////////Variables//////////////////////////////////////////

	const char * mFileName;
	mutable FbxString WindowMessage;

	mutable FbxTime mFrameTime, mStart, mStop, mCurrentTime;
	mutable FbxTime mCache_Start, mCache_Stop;

	FbxManager * SdkManager;
	FbxScene * Scene;
	FbxImporter * Importer;
	FbxAnimLayer * CurrentAnimLayer;
	FbxNode * SelectedNode;

	int mPoseIndex;
	bool SupportVBO;

	FbxArray<FbxString*> mAnimStackNameArray;


	/////////////////////////////////////////////////////Methods  //////////////////////////////////////////
	FBXLoader(const char * pFileName, int pWindowWidth, int pWindowHeight);
	~FBXLoader(void);

	void FillPoseArray(FbxScene* pScene, FbxArray<FbxPose*>& pPoseArray);

	void PreparePointCacheData(FbxScene* pScene, FbxTime &pCache_Start, FbxTime &pCache_Stop);

	bool LoadTextureFromFile(const FbxString & pFilePath, unsigned int & pTextureObject);//done

	void LoadCacheRecursive(FbxNode * pNode, FbxAnimLayer * pAnimLayer, bool pSupportVBO);//done

	void UnloadCacheRecursive(FbxNode * pNode); //done

	void LoadCacheRecursive(FbxScene * pScene, FbxAnimLayer * pAnimLayer, const char * pFbxFileName, bool pSupportVBO);//done

	void UnloadCacheRecursive(FbxScene * pScene);//done

	bool LoadFile();//done

	bool SetCurrentAnimStack(int pIndex);

	void OnTimerClick() const;

	bool Draw();

	void DrawNodeRecursive(FbxNode* pNode, FbxTime& pTime, FbxAnimLayer* pAnimLayer,
		FbxAMatrix& pParentGlobalPosition, FbxPose* pPose);

	FbxAMatrix GetGlobalPosition(FbxNode* pNode, const FbxTime& pTime, FbxPose* pPose = NULL, FbxAMatrix* pParentGlobalPosition = NULL);

	FbxAMatrix GetPoseMatrix(FbxPose* pPose, int pNodeIndex);

	FbxAMatrix GetGeometry(FbxNode* pNode);

	void DrawNode(FbxNode* pNode, FbxTime& pTime,FbxAnimLayer* pAnimLayer,FbxAMatrix& pParentGlobalPosition,
		FbxAMatrix& pGlobalPosition,FbxPose* pPose);

	void DrawSkeleton(FbxNode* pNode, FbxAMatrix& pParentGlobalPosition, FbxAMatrix& pGlobalPosition);

	void DrawMesh(FbxNode* pNode, FbxTime& pTime, FbxAnimLayer* pAnimLayer,
		FbxAMatrix& pGlobalPosition, FbxPose* pPose);

	void GlDrawLimbNode(FbxAMatrix& pGlobalBasePosition, FbxAMatrix& pGlobalEndPosition);


	void ReadVertexCacheData(FbxMesh* pMesh, FbxTime& pTime, FbxVector4* pVertexArray);

	void ComputeShapeDeformation(FbxMesh* pMesh, FbxTime& pTime, FbxAnimLayer * pAnimLayer, FbxVector4* pVertexArray);

	void ComputeSkinDeformation(FbxAMatrix& pGlobalPosition, FbxMesh* pMesh,  FbxTime& pTime, FbxVector4* pVertexArray, FbxPose* pPose);

	void ComputeLinearDeformation(FbxAMatrix& pGlobalPosition, FbxMesh* pMesh, FbxTime& pTime, FbxVector4* pVertexArray, FbxPose* pPose);

	void ComputeClusterDeformation(FbxAMatrix& pGlobalPosition, 
		FbxMesh* pMesh,FbxCluster* pCluster, FbxAMatrix& pVertexTransformMatrix,FbxTime pTime, FbxPose* pPose);

	void MatrixScale(FbxAMatrix& pMatrix, double pValue);

	void MatrixAddToDiagonal(FbxAMatrix& pMatrix, double pValue);

	void MatrixAdd(FbxAMatrix& pDstMatrix, FbxAMatrix& pSrcMatrix);

	void ComputeDualQuaternionDeformation(FbxAMatrix& pGlobalPosition, 
		FbxMesh* pMesh, FbxTime& pTime,FbxVector4* pVertexArray,FbxPose* pPose);

	void SetSelectedNode(FbxNode * pSelectedNode);//done

	void InitializeSdkObjects(FbxManager*& pManager, FbxScene*& pScene);//done

	void DestroySdkObjects(FbxManager* pManager, bool pExitStatus);

	// The time period for one frame.
    const FbxTime GetFrameTime() const { return mFrameTime; }

	void DisplayGrid(const FbxAMatrix & pTransform);  //optional.


};


const int TRIANGLE_VERTEX_COUNT = 3;

// Four floats for every position.
const int VERTEX_STRIDE = 4;
// Three floats for every normal.
const int NORMAL_STRIDE = 3;
// Two floats for every UV.
const int UV_STRIDE = 2;


// Save mesh vertices, normals, UVs and indices in GPU with OpenGL Vertex Buffer Objects
class VBOMesh
{
public:
	VBOMesh();
	~VBOMesh();

	// Save up data into GPU buffers.
	bool Initialize(const FbxMesh * pMesh);

	// Update vertex positions for deformed meshes.
	void UpdateVertexPosition(const FbxMesh * pMesh, const FbxVector4 * pVertices) const;

	// Bind buffers, set vertex arrays, turn on lighting and texture.
	void BeginDraw() const;
	// Draw all the faces with specific material with given shading mode.
	void Draw(int pMaterialIndex) const;
	// Unbind buffers, reset vertex arrays, turn off lighting and texture.
	void EndDraw() const;

	// Get the count of material groups
	int GetSubMeshCount() const { return mSubMeshes.GetCount(); }

private:
	enum
	{
		VERTEX_VBO,
		NORMAL_VBO,
		UV_VBO,
		INDEX_VBO,
		VBO_COUNT,
	};

	// For every material, record the offsets in every VBO and triangle counts
	struct SubMesh
	{
		SubMesh() : IndexOffset(0), TriangleCount(0) {}

		int IndexOffset;
		int TriangleCount;
	};

	GLuint mVBONames[VBO_COUNT];
	FbxArray<SubMesh*> mSubMeshes;
	bool mHasNormal;
	bool mHasUV;
	bool mAllByControlPoint; // Save data in VBO by control point or by polygon vertex.
};



// Cache for FBX material
class MaterialCache
{
public:
	MaterialCache();
	~MaterialCache();

	bool Initialize(const FbxSurfaceMaterial * pMaterial);

	// Set material colors and binding diffuse texture if exists.
	void SetCurrentMaterial() const;

	bool HasTexture() const { return mDiffuse.mTextureName != 0; }

	// Set default green color.
	static void SetDefaultMaterial();

	// Get specific property value and connected texture if any.
	// Value = Property value * Factor property value (if no factor property, multiply by 1).
	FbxDouble3 GetMaterialProperty(const FbxSurfaceMaterial * pMaterial,
		const char * pPropertyName,
		const char * pFactorPropertyName,
		GLuint & pTextureName)
	{
		FbxDouble3 lResult(0, 0, 0);
		const FbxProperty lProperty = pMaterial->FindProperty(pPropertyName);
		const FbxProperty lFactorProperty = pMaterial->FindProperty(pFactorPropertyName);
		if (lProperty.IsValid() && lFactorProperty.IsValid())
		{
			lResult = lProperty.Get<FbxDouble3>();
			double lFactor = lFactorProperty.Get<FbxDouble>();
			if (lFactor != 1)
			{
				lResult[0] *= lFactor;
				lResult[1] *= lFactor;
				lResult[2] *= lFactor;
			}
		}

		if (lProperty.IsValid())
		{
			const int lTextureCount = lProperty.GetSrcObjectCount<FbxFileTexture>();
			if (lTextureCount)
			{
				const FbxFileTexture* lTexture = lProperty.GetSrcObject<FbxFileTexture>();
				if (lTexture && lTexture->GetUserDataPtr())
				{
					pTextureName = *(static_cast<GLuint *>(lTexture->GetUserDataPtr()));
				}
			}
		}

		return lResult;
	}

private:
	struct ColorChannel
	{
		ColorChannel() : mTextureName(0)
		{
			mColor[0] = 0.0f;
			mColor[1] = 0.0f;
			mColor[2] = 0.0f;
			mColor[3] = 1.0f;
		}

		GLuint mTextureName;
		GLfloat mColor[4];
	};
	ColorChannel mEmissive;
	ColorChannel mAmbient;
	ColorChannel mDiffuse;
	ColorChannel mSpecular;
	GLfloat mShinness;
};


// Property cache, value and animation curve.
struct PropertyChannel
{
	PropertyChannel() : mAnimCurve(NULL), mValue(0.0f) {}
	// Query the channel value at specific time.
	GLfloat Get(const FbxTime & pTime) const
	{
		if (mAnimCurve)
		{
			return mAnimCurve->Evaluate(pTime);
		}
		else
		{
			return mValue;
		}
	}

	FbxAnimCurve * mAnimCurve;
	GLfloat mValue;
};