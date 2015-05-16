#pragma once
#include "FBXLoader.h"


FBXLoader::FBXLoader(const char * pFileName, int pWindowWidth, int pWindowHeight)
	: mFileName(pFileName),SdkManager(NULL), Scene(NULL),Importer(NULL), CurrentAnimLayer(NULL),SelectedNode(NULL),
	  mPoseIndex(-1),SupportVBO(true)

{
	if (mFileName == NULL)
		mFileName = NULL;

	// initialize cache start and stop time
	mCache_Start = FBXSDK_TIME_INFINITE;
	mCache_Stop  = FBXSDK_TIME_MINUS_INFINITE;

	// Create the FBX SDK manager which is the object allocator for almost 
	// all the classes in the SDK and create the scene.
	InitializeSdkObjects(SdkManager, Scene);

	if (SdkManager)
	{
		// Create the importer.
		int lFileFormat = -1;
		Importer = FbxImporter::Create(SdkManager,"");
		if (!SdkManager->GetIOPluginRegistry()->DetectReaderFileFormat(mFileName, lFileFormat) )
		{
			// Unrecognizable file format. Try to fall back to FbxImporter::eFBX_BINARY
			lFileFormat = SdkManager->GetIOPluginRegistry()->FindReaderIDByDescription( "FBX binary (*.fbx)" );;
		}

		// Initialize the importer by providing a filename.
		if(Importer->Initialize(mFileName, lFileFormat) == true)
		{
			// The file is going to be imported at 
			// the end of the first display callback.
			WindowMessage = "Importing file ";
			WindowMessage += mFileName;
			WindowMessage += "\nPlease wait!";

		}
		else
		{
			WindowMessage = "Unable to open file ";
			WindowMessage += mFileName;
			WindowMessage += "\nError reported: ";
			WindowMessage += Importer->GetStatus().GetErrorString();
			WindowMessage += "\nEsc to exit";
		}
	}
	else
	{
		WindowMessage = "Unable to create the FBX SDK manager";
		WindowMessage += "\nEsc to exit";
	}

}


FBXLoader::~FBXLoader(void)
{

	FbxArrayDelete(mAnimStackNameArray);

	// Unload the cache and free the memory
	if (Scene)
	{
		UnloadCacheRecursive(Scene);
	}

	// Delete the FBX SDK manager. All the objects that have been allocated 
	// using the FBX SDK manager and that haven't been explicitly destroyed 
	// are automatically destroyed at the same time.
	DestroySdkObjects(SdkManager, true);
}

void  FBXLoader::FillPoseArray(FbxScene* pScene, FbxArray<FbxPose*>& pPoseArray)
{
	const int lPoseCount = pScene->GetPoseCount();

	for (int i=0; i < lPoseCount; ++i)
	{
		pPoseArray.Add(pScene->GetPose(i));
	}
}


void FBXLoader::PreparePointCacheData(FbxScene* pScene, FbxTime &pCache_Start, FbxTime &pCache_Stop)
{

	// This function show how to cycle through scene elements in a linear way.
	const int lNodeCount = pScene->GetSrcObjectCount<FbxNode>();
	FbxStatus lStatus;

	for (int lIndex=0; lIndex<lNodeCount; lIndex++)
	{
		FbxNode* lNode = pScene->GetSrcObject<FbxNode>(lIndex);

		if (lNode->GetGeometry()) 
		{
			int i, lVertexCacheDeformerCount = lNode->GetGeometry()->GetDeformerCount(FbxDeformer::eVertexCache);

			// There should be a maximum of 1 Vertex Cache Deformer for the moment
			lVertexCacheDeformerCount = lVertexCacheDeformerCount > 0 ? 1 : 0;

			for (i=0; i<lVertexCacheDeformerCount; ++i )
			{
				// Get the Point Cache object
				FbxVertexCacheDeformer* lDeformer = static_cast<FbxVertexCacheDeformer*>(lNode->GetGeometry()->GetDeformer(i, FbxDeformer::eVertexCache));
				if( !lDeformer ) continue;
				FbxCache* lCache = lDeformer->GetCache();
				if( !lCache ) continue;

				// Process the point cache data only if the constraint is active
				if (lDeformer->IsActive())
				{
					if (lCache->GetCacheFileFormat() == FbxCache::eMaxPointCacheV2)
					{
						// This code show how to convert from PC2 to MC point cache format
						// turn it on if you need it.
#if 0 
						if (!lCache->ConvertFromPC2ToMC(FbxCache::eMCOneFile, 
							FbxTime::GetFrameRate(pScene->GetGlobalTimeSettings().GetTimeMode())))
						{
							// Conversion failed, retrieve the error here
							FbxString lTheErrorIs = lCache->GetStaus().GetErrorString();
						}
#endif
					}
					else if (lCache->GetCacheFileFormat() == FbxCache::eMayaCache)
					{
						// This code show how to convert from MC to PC2 point cache format
						// turn it on if you need it.
						//#if 0 
						if (!lCache->ConvertFromMCToPC2(FbxTime::GetFrameRate(pScene->GetGlobalSettings().GetTimeMode()), 0, &lStatus))
						{
							// Conversion failed, retrieve the error here
							FbxString lTheErrorIs = lStatus.GetErrorString();
						}
						//#endif
					}


					// Now open the cache file to read from it
					if (!lCache->OpenFileForRead(&lStatus))
					{
						// Cannot open file 
						FbxString lTheErrorIs = lStatus.GetErrorString();

						// Set the deformer inactive so we don't play it back
						lDeformer->SetActive(false);
					}
					else
					{
						// get the start and stop time of the cache
						int lChannelCount = lCache->GetChannelCount();

						for (int iChannelNo=0; iChannelNo < lChannelCount; iChannelNo++)
						{
							FbxTime lChannel_Start;
							FbxTime lChannel_Stop;

							if(lCache->GetAnimationRange(iChannelNo, lChannel_Start, lChannel_Stop))
							{
								// get the smallest start time
								if(lChannel_Start < pCache_Start) pCache_Start = lChannel_Start;

								// get the biggest stop time
								if(lChannel_Stop  > pCache_Stop)  pCache_Stop  = lChannel_Stop;
							}
						}
					}
				}
			}
		}
	}

}

bool FBXLoader::LoadTextureFromFile(const FbxString & pFilePath, unsigned int & pTextureObject)
{
	if (pFilePath.Right(3).Upper() == "TGA")
	{
		tga_image lTGAImage;

		if (tga_read(&lTGAImage, pFilePath.Buffer()) == TGA_NOERR)
		{
			// Make sure the image is left to right
			if (tga_is_right_to_left(&lTGAImage))
				tga_flip_horiz(&lTGAImage);

			// Make sure the image is bottom to top
			if (tga_is_top_to_bottom(&lTGAImage))
				tga_flip_vert(&lTGAImage);

			// Make the image BGR 24
			tga_convert_depth(&lTGAImage, 24);

			// Transfer the texture date into GPU
			glGenTextures(1, &pTextureObject);
			glBindTexture(GL_TEXTURE_2D, pTextureObject);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glTexImage2D(GL_TEXTURE_2D, 0, 3, lTGAImage.width, lTGAImage.height, 0, GL_BGR,
				GL_UNSIGNED_BYTE, lTGAImage.image_data);
			glBindTexture(GL_TEXTURE_2D, 0);

			tga_free_buffers(&lTGAImage);

			return true;
		}
	}

	return false;
}

void FBXLoader::LoadCacheRecursive(FbxNode * pNode, FbxAnimLayer * pAnimLayer, bool pSupportVBO)
{

	// Bake material and hook as user data.
	const int lMaterialCount = pNode->GetMaterialCount();
	for (int lMaterialIndex = 0; lMaterialIndex < lMaterialCount; ++lMaterialIndex)
	{
		FbxSurfaceMaterial * lMaterial = pNode->GetMaterial(lMaterialIndex);
		if (lMaterial && !lMaterial->GetUserDataPtr())
		{
			FbxAutoPtr<MaterialCache> lMaterialCache(new MaterialCache);
			if (lMaterialCache->Initialize(lMaterial))
			{
				lMaterial->SetUserDataPtr(lMaterialCache.Release());
			}
		}
	}

	FbxNodeAttribute* lNodeAttribute = pNode->GetNodeAttribute();
	if (lNodeAttribute)
	{
		// Bake mesh as VBO(vertex buffer object) into GPU.
		if (lNodeAttribute->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			FbxMesh * lMesh = pNode->GetMesh();
			if (pSupportVBO && lMesh && !lMesh->GetUserDataPtr())
			{
				FbxAutoPtr<VBOMesh> lMeshCache(new VBOMesh);
				if (lMeshCache->Initialize(lMesh))
				{
					lMesh->SetUserDataPtr(lMeshCache.Release());
				}
			}
		}
	}

	const int lChildCount = pNode->GetChildCount();
	for (int lChildIndex = 0; lChildIndex < lChildCount; ++lChildIndex)
	{
		LoadCacheRecursive(pNode->GetChild(lChildIndex), pAnimLayer, pSupportVBO);
	}

}


void FBXLoader::UnloadCacheRecursive(FbxNode * pNode)
{
	// Unload the material cache
	const int lMaterialCount = pNode->GetMaterialCount();
	for (int lMaterialIndex = 0; lMaterialIndex < lMaterialCount; ++lMaterialIndex)
	{
		FbxSurfaceMaterial * lMaterial = pNode->GetMaterial(lMaterialIndex);
		if (lMaterial && lMaterial->GetUserDataPtr())
		{
			MaterialCache * lMaterialCache = static_cast<MaterialCache *>(lMaterial->GetUserDataPtr());
			lMaterial->SetUserDataPtr(NULL);
			delete lMaterialCache;
		}
	}

	FbxNodeAttribute* lNodeAttribute = pNode->GetNodeAttribute();
	if (lNodeAttribute)
	{
		// Unload the mesh cache
		if (lNodeAttribute->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			FbxMesh * lMesh = pNode->GetMesh();
			if (lMesh && lMesh->GetUserDataPtr())
			{
				VBOMesh * lMeshCache = static_cast<VBOMesh *>(lMesh->GetUserDataPtr());
				lMesh->SetUserDataPtr(NULL);
				delete lMeshCache;
			}
		}
	}

	const int lChildCount = pNode->GetChildCount();
	for (int lChildIndex = 0; lChildIndex < lChildCount; ++lChildIndex)
	{
		UnloadCacheRecursive(pNode->GetChild(lChildIndex));
	}
}

void FBXLoader::LoadCacheRecursive(FbxScene * pScene, FbxAnimLayer * pAnimLayer, const char * pFbxFileName, bool pSupportVBO)
{
	// Load the textures into GPU, only for file texture now
	const int lTextureCount = pScene->GetTextureCount();
	for (int lTextureIndex = 0; lTextureIndex < lTextureCount; ++lTextureIndex)
	{
		FbxTexture * lTexture = pScene->GetTexture(lTextureIndex);
		FbxFileTexture * lFileTexture = FbxCast<FbxFileTexture>(lTexture);
		if (lFileTexture && !lFileTexture->GetUserDataPtr())
		{
			// Try to load the texture from absolute path
			const FbxString lFileName = lFileTexture->GetFileName();

			// Only TGA textures are supported now.
			if (lFileName.Right(3).Upper() != "TGA")
			{
				FBXSDK_printf("Only TGA textures are supported now: %s\n", lFileName.Buffer());
				continue;
			}

			GLuint lTextureObject = 0;
			bool lStatus =  LoadTextureFromFile(lFileName, lTextureObject); //<<---texture

			const FbxString lAbsFbxFileName = FbxPathUtils::Resolve(pFbxFileName);
			const FbxString lAbsFolderName = FbxPathUtils::GetFolderName(lAbsFbxFileName);
			if (!lStatus)
			{
				// Load texture from relative file name (relative to FBX file)
				const FbxString lResolvedFileName = FbxPathUtils::Bind(lAbsFolderName, lFileTexture->GetRelativeFileName());
				lStatus = LoadTextureFromFile(lResolvedFileName, lTextureObject);// <<---texture
			}

			if (!lStatus)
			{
				// Load texture from file name only (relative to FBX file)
				const FbxString lTextureFileName = FbxPathUtils::GetFileName(lFileName);
				const FbxString lResolvedFileName = FbxPathUtils::Bind(lAbsFolderName, lTextureFileName);
				lStatus = LoadTextureFromFile(lResolvedFileName, lTextureObject); //<<------texture
			}

			if (!lStatus)
			{
				FBXSDK_printf("Failed to load texture file: %s\n", lFileName.Buffer());
				continue;
			}

			if (lStatus)
			{
				GLuint * lTextureName = new GLuint(lTextureObject);
				lFileTexture->SetUserDataPtr(lTextureName);
			}
		}
	}

	LoadCacheRecursive(pScene->GetRootNode(), pAnimLayer, pSupportVBO);
}

void FBXLoader::UnloadCacheRecursive(FbxScene * pScene)
{
	const int lTextureCount = pScene->GetTextureCount();
	for (int lTextureIndex = 0; lTextureIndex < lTextureCount; ++lTextureIndex)
	{
		FbxTexture * lTexture = pScene->GetTexture(lTextureIndex);
		FbxFileTexture * lFileTexture = FbxCast<FbxFileTexture>(lTexture);
		if (lFileTexture && lFileTexture->GetUserDataPtr())
		{
			GLuint * lTextureName = static_cast<GLuint *>(lFileTexture->GetUserDataPtr());
			lFileTexture->SetUserDataPtr(NULL);
			glDeleteTextures(1, lTextureName);
			delete lTextureName;
		}
	}

	UnloadCacheRecursive(pScene->GetRootNode());
}

bool FBXLoader::LoadFile()
{
	bool lResult = false;
	// Make sure that the scene is ready to load.

	if (Importer->Import(Scene) == true)
	{
		// Set the scene status flag to refresh 
		// the scene in the first timer callback.

		// Convert Axis System to what is used in this example, if needed
		FbxAxisSystem SceneAxisSystem = Scene->GetGlobalSettings().GetAxisSystem();
		FbxAxisSystem OurAxisSystem(FbxAxisSystem::eYAxis, FbxAxisSystem::eParityOdd, FbxAxisSystem::eRightHanded);
		if( SceneAxisSystem != OurAxisSystem )
		{
			OurAxisSystem.ConvertScene(Scene);
		}

		// Convert Unit System to what is used in this example, if needed
		FbxSystemUnit SceneSystemUnit = Scene->GetGlobalSettings().GetSystemUnit();
		if( SceneSystemUnit.GetScaleFactor() != 1.0 )
		{
			//The unit in this example is centimeter.
			FbxSystemUnit::cm.ConvertScene( Scene);
		}


		// Get the list of all the animation stack.
		Scene->FillAnimStackNameArray(mAnimStackNameArray);

		// Get the list of all the cameras in the scene.
		// FillCameraArray(mScene, mCameraArray);

		// Convert mesh, NURBS and patch into triangle mesh
		FbxGeometryConverter lGeomConverter(SdkManager);



		lGeomConverter.Triangulate(Scene,true,true);//node //<<------------------------------------------timing more..

		// Split meshes per material, so that we only have one material per mesh (for VBO support)
		lGeomConverter.SplitMeshesPerMaterial(Scene, /*replace*/true);

		// Bake the scene for one frame
		LoadCacheRecursive(Scene, CurrentAnimLayer, mFileName, SupportVBO);


		// Initialize the frame period.
		mFrameTime.SetTime(0, 0, 0, 1, 0, Scene->GetGlobalSettings().GetTimeMode());

		lResult = true;
	}
	else
	{
		// Import failed, set the scene status flag accordingly.

		WindowMessage = "Unable to import file ";
		WindowMessage += mFileName;
		WindowMessage += "\nError reported: ";
		WindowMessage += Importer->GetStatus().GetErrorString();
	}

	// Destroy the importer to release the file.
	Importer->Destroy();
	Importer = NULL;

	return lResult;
}

bool FBXLoader::SetCurrentAnimStack(int pIndex)
{
	const int lAnimStackCount = mAnimStackNameArray.GetCount();
	if (!lAnimStackCount || pIndex >= lAnimStackCount)
	{
		return false;
	}

	// select the base layer from the animation stack
	FbxAnimStack * lCurrentAnimationStack = Scene->FindMember<FbxAnimStack>(mAnimStackNameArray[pIndex]->Buffer());
	if (lCurrentAnimationStack == NULL)
	{
		// this is a problem. The anim stack should be found in the scene!
		return false;
	}

	// we assume that the first animation layer connected to the animation stack is the base layer
	// (this is the assumption made in the FBXSDK)
	CurrentAnimLayer = lCurrentAnimationStack->GetMember<FbxAnimLayer>();
	Scene->SetCurrentAnimationStack(lCurrentAnimationStack);

	FbxTakeInfo* lCurrentTakeInfo = Scene->GetTakeInfo(*(mAnimStackNameArray[pIndex]));
	if (lCurrentTakeInfo)
	{
		mStart = lCurrentTakeInfo->mLocalTimeSpan.GetStart();
		mStop = lCurrentTakeInfo->mLocalTimeSpan.GetStop();
	}
	else
	{
		// Take the time line value
		FbxTimeSpan lTimeLineTimeSpan;
		Scene->GetGlobalSettings().GetTimelineDefaultTimeSpan(lTimeLineTimeSpan);

		mStart = lTimeLineTimeSpan.GetStart();
		mStop  = lTimeLineTimeSpan.GetStop();
	}

	// check for smallest start with cache start
	if(mCache_Start < mStart)
		mStart = mCache_Start;

	// check for biggest stop with cache stop
	if(mCache_Stop  > mStop)  
		mStop  = mCache_Stop;

	// move to beginning
	mCurrentTime = mStart;

	return true;
}

void FBXLoader::OnTimerClick() const
{
	// Loop in the animation stack if not paused.
	if (mStop > mStart)
	{

		mCurrentTime += mFrameTime;

		if (mCurrentTime > mStop)
		{
			mCurrentTime = mStart;
		}
	}
}

//change by gl version..
bool FBXLoader::Draw()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.0,0.0,0.0,0);


	glPushAttrib(GL_ENABLE_BIT);
	glPushAttrib(GL_LIGHTING_BIT);
	glEnable(GL_DEPTH_TEST);
	// Draw the front face only, except for the texts and lights.
	glEnable(GL_CULL_FACE);


	FbxPose * lPose = NULL;
	if (mPoseIndex != -1)
	{
		lPose = Scene->GetPose(mPoseIndex);
	}

	// If one node is selected, draw it and its children.
	FbxAMatrix lDummyGlobalPosition;

	if (SelectedNode)
	{
		glScalef(5,5,5);
		DrawNodeRecursive(SelectedNode, mCurrentTime, CurrentAnimLayer, lDummyGlobalPosition, lPose);
		DisplayGrid(lDummyGlobalPosition);
	}
	// Otherwise, draw the whole scene.
	else
	{
		glScalef(15,15,15);
		DrawNodeRecursive(Scene->GetRootNode(), mCurrentTime, CurrentAnimLayer, lDummyGlobalPosition, lPose);
		DisplayGrid(lDummyGlobalPosition);
	}

	glPopAttrib();
	glPopAttrib();

	return true;
}

void FBXLoader::DrawNodeRecursive(FbxNode* pNode, FbxTime& pTime, FbxAnimLayer* pAnimLayer,
								  FbxAMatrix& pParentGlobalPosition, FbxPose* pPose)
{

	FbxAMatrix lGlobalPosition = GetGlobalPosition(pNode, pTime, pPose, &pParentGlobalPosition);

	if (pNode->GetNodeAttribute())
	{
		// Geometry offset.
		// it is not inherited by the children.
		FbxAMatrix lGeometryOffset = GetGeometry(pNode);
		FbxAMatrix lGlobalOffPosition = lGlobalPosition * lGeometryOffset;

		DrawNode(pNode, pTime, pAnimLayer, pParentGlobalPosition, lGlobalOffPosition, pPose);
	}

	const int lChildCount = pNode->GetChildCount();
	for (int lChildIndex = 0; lChildIndex < lChildCount; ++lChildIndex)
	{
		DrawNodeRecursive(pNode->GetChild(lChildIndex), pTime, pAnimLayer, lGlobalPosition, pPose);
	}

}


FbxAMatrix  FBXLoader::GetGlobalPosition(FbxNode* pNode, const FbxTime& pTime, FbxPose* pPose, FbxAMatrix* pParentGlobalPosition)
{

	FbxAMatrix lGlobalPosition;
	bool        lPositionFound = false;

	if (pPose)
	{
		int lNodeIndex = pPose->Find(pNode);

		if (lNodeIndex > -1)
		{
			// The bind pose is always a global matrix.
			// If we have a rest pose, we need to check if it is
			// stored in global or local space.
			if (pPose->IsBindPose() || !pPose->IsLocalMatrix(lNodeIndex))
			{
				lGlobalPosition = GetPoseMatrix(pPose, lNodeIndex);
			}
			else
			{
				// We have a local matrix, we need to convert it to
				// a global space matrix.
				FbxAMatrix lParentGlobalPosition;

				if (pParentGlobalPosition)
				{
					lParentGlobalPosition = *pParentGlobalPosition;
				}
				else
				{
					if (pNode->GetParent())
					{
						lParentGlobalPosition = GetGlobalPosition(pNode->GetParent(), pTime, pPose);
					}
				}

				FbxAMatrix lLocalPosition = GetPoseMatrix(pPose, lNodeIndex);
				lGlobalPosition = lParentGlobalPosition * lLocalPosition;
			}

			lPositionFound = true;
		}
	}

	if (!lPositionFound)
	{
		// There is no pose entry for that node, get the current global position instead.

		// Ideally this would use parent global position and local position to compute the global position.
		// Unfortunately the equation 
		//    lGlobalPosition = pParentGlobalPosition * lLocalPosition
		// does not hold when inheritance type is other than "Parent" (RSrs).
		// To compute the parent rotation and scaling is tricky in the RrSs and Rrs cases.
		lGlobalPosition = pNode->EvaluateGlobalTransform(pTime);
	}

	return lGlobalPosition;
}

FbxAMatrix FBXLoader::GetPoseMatrix(FbxPose* pPose, int pNodeIndex)
{
	FbxAMatrix lPoseMatrix;
	FbxMatrix lMatrix = pPose->GetMatrix(pNodeIndex);

	memcpy((double*)lPoseMatrix, (double*)lMatrix, sizeof(lMatrix.mData));

	return lPoseMatrix;
}

FbxAMatrix FBXLoader::GetGeometry(FbxNode* pNode)
{
	const FbxVector4 lT = pNode->GetGeometricTranslation(FbxNode::eSourcePivot);
	const FbxVector4 lR = pNode->GetGeometricRotation(FbxNode::eSourcePivot);
	const FbxVector4 lS = pNode->GetGeometricScaling(FbxNode::eSourcePivot);

	return FbxAMatrix(lT, lR, lS);
}

void FBXLoader::DrawNode(FbxNode* pNode, FbxTime& pTime,FbxAnimLayer* pAnimLayer,FbxAMatrix& pParentGlobalPosition,
						 FbxAMatrix& pGlobalPosition,FbxPose* pPose)
{
	FbxNodeAttribute* lNodeAttribute = pNode->GetNodeAttribute();
	if (lNodeAttribute)
	{
		if (lNodeAttribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
		{
			DrawSkeleton(pNode, pParentGlobalPosition, pGlobalPosition);
		}
		// NURBS and patch have been converted into triangluation meshes.
		else if (lNodeAttribute->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			DrawMesh(pNode, pTime, pAnimLayer, pGlobalPosition, pPose);
		}
		else if (lNodeAttribute->GetAttributeType() == FbxNodeAttribute::eNull)
		{
		}
	}
	else
	{
		// Draw a Null for nodes without attribute.
	}

}


void FBXLoader::DrawSkeleton(FbxNode* pNode, FbxAMatrix& pParentGlobalPosition, FbxAMatrix& pGlobalPosition)
{
	FbxSkeleton* lSkeleton = (FbxSkeleton*) pNode->GetNodeAttribute();

	// Only draw the skeleton if it's a limb node and if 
	// the parent also has an attribute of type skeleton.
	if (lSkeleton->GetSkeletonType() == FbxSkeleton::eLimbNode &&
		pNode->GetParent() &&
		pNode->GetParent()->GetNodeAttribute() &&
		pNode->GetParent()->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton)
	{
		GlDrawLimbNode(pParentGlobalPosition, pGlobalPosition); 
	}
}


void FBXLoader::DrawMesh(FbxNode* pNode, FbxTime& pTime, FbxAnimLayer* pAnimLayer,FbxAMatrix& pGlobalPosition, FbxPose* pPose)
{

	FbxMesh* lMesh = pNode->GetMesh();
	const int lVertexCount = lMesh->GetControlPointsCount();

	// No vertex to draw.
	if (lVertexCount == 0)
	{
		return;
	}

	const VBOMesh * lMeshCache = static_cast<const VBOMesh *>(lMesh->GetUserDataPtr());

	// If it has some defomer connection, update the vertices position
	const bool lHasVertexCache = lMesh->GetDeformerCount(FbxDeformer::eVertexCache) &&
		(static_cast<FbxVertexCacheDeformer*>(lMesh->GetDeformer(0, FbxDeformer::eVertexCache)))->IsActive();
	const bool lHasShape = lMesh->GetShapeCount() > 0;
	const bool lHasSkin = lMesh->GetDeformerCount(FbxDeformer::eSkin) > 0;
	const bool lHasDeformation = lHasVertexCache || lHasShape || lHasSkin;

	FbxVector4* lVertexArray = NULL;
	if (!lMeshCache || lHasDeformation)
	{
		lVertexArray = new FbxVector4[lVertexCount];
		memcpy(lVertexArray, lMesh->GetControlPoints(), lVertexCount * sizeof(FbxVector4));
	}

	if (lHasDeformation)
	{
		// Active vertex cache deformer will overwrite any other deformer
		if (lHasVertexCache)
		{
			ReadVertexCacheData(lMesh, pTime, lVertexArray);
		}
		else
		{
			if (lHasShape)
			{
				// Deform the vertex array with the shapes.
				ComputeShapeDeformation(lMesh, pTime, pAnimLayer, lVertexArray);
			}

			//we need to get the number of clusters
			const int lSkinCount = lMesh->GetDeformerCount(FbxDeformer::eSkin);
			int lClusterCount = 0;
			for (int lSkinIndex = 0; lSkinIndex < lSkinCount; ++lSkinIndex)
			{
				lClusterCount += ((FbxSkin *)(lMesh->GetDeformer(lSkinIndex, FbxDeformer::eSkin)))->GetClusterCount();
			}
			if (lClusterCount)
			{
				// Deform the vertex array with the skin deformer.
				ComputeSkinDeformation(pGlobalPosition, lMesh, pTime, lVertexArray, pPose);
			}
		}

		if (lMeshCache)
			lMeshCache->UpdateVertexPosition(lMesh, lVertexArray);
	}

	glPushMatrix();
	glMultMatrixd((const double*)pGlobalPosition);

	if (lMeshCache)
	{
		lMeshCache->BeginDraw();
		const int lSubMeshCount = lMeshCache->GetSubMeshCount();
		for (int lIndex = 0; lIndex < lSubMeshCount; ++lIndex)
		{

			const FbxSurfaceMaterial * lMaterial = pNode->GetMaterial(lIndex);
			if (lMaterial)
			{
				const MaterialCache * lMaterialCache = static_cast<const MaterialCache *>(lMaterial->GetUserDataPtr());
				if (lMaterialCache)
				{
					lMaterialCache->SetCurrentMaterial();
				}
			}
			else
			{
				// Draw green for faces without material
				MaterialCache::SetDefaultMaterial();
			}


			lMeshCache->Draw(lIndex);
		}
		lMeshCache->EndDraw();
	}
	else
	{
		// OpenGL driver is too lower and use Immediate Mode
		glColor4f(0.5f, 0.5f, 0.5f, 1.0f);
		const int lPolygonCount = lMesh->GetPolygonCount();
		for (int lPolygonIndex = 0; lPolygonIndex < lPolygonCount; lPolygonIndex++)
		{
			const int lVerticeCount = lMesh->GetPolygonSize(lPolygonIndex);
			glBegin(GL_LINE_LOOP);
			for (int lVerticeIndex = 0; lVerticeIndex < lVerticeCount; lVerticeIndex++)
			{
				glVertex3dv((GLdouble *)lVertexArray[lMesh->GetPolygonVertex(lPolygonIndex, lVerticeIndex)]);
			}
			glEnd();
		}
	}

	glPopMatrix();

	delete [] lVertexArray;

}


void FBXLoader::GlDrawLimbNode(FbxAMatrix& pGlobalBasePosition, FbxAMatrix& pGlobalEndPosition)
{
	glColor3f(1.0, 0.0, 0.0);
	glLineWidth(2.0);

	glBegin(GL_LINES);

	glVertex3dv((GLdouble *)pGlobalBasePosition.GetT());
	glVertex3dv((GLdouble *)pGlobalEndPosition.GetT());

	glEnd();
}


void FBXLoader::ReadVertexCacheData(FbxMesh* pMesh,  FbxTime& pTime, FbxVector4* pVertexArray)
{

	FbxVertexCacheDeformer* lDeformer     = static_cast<FbxVertexCacheDeformer*>(pMesh->GetDeformer(0, FbxDeformer::eVertexCache));
	FbxCache*               lCache        = lDeformer->GetCache();
	int                      lChannelIndex = -1;
	unsigned int             lVertexCount  = (unsigned int)pMesh->GetControlPointsCount();
	bool                     lReadSucceed  = false;
	double*                  lReadBuf      = new double[3*lVertexCount];

	if (lCache->GetCacheFileFormat() == FbxCache::eMayaCache)
	{
		if ((lChannelIndex = lCache->GetChannelIndex(lDeformer->GetCacheChannel())) > -1)
		{
			lReadSucceed = lCache->Read(lChannelIndex, pTime, lReadBuf, lVertexCount);
		}
	}
	else // eMaxPointCacheV2
	{
		lReadSucceed = lCache->Read((unsigned int)pTime.GetFrameCount(), lReadBuf, lVertexCount);
	}

	if (lReadSucceed)
	{
		unsigned int lReadBufIndex = 0;

		while (lReadBufIndex < 3*lVertexCount)
		{
			// In statements like "pVertexArray[lReadBufIndex/3].SetAt(2, lReadBuf[lReadBufIndex++])", 
			// on Mac platform, "lReadBufIndex++" is evaluated before "lReadBufIndex/3". 
			// So separate them.
			pVertexArray[lReadBufIndex/3].mData[0] = lReadBuf[lReadBufIndex]; lReadBufIndex++;
			pVertexArray[lReadBufIndex/3].mData[1] = lReadBuf[lReadBufIndex]; lReadBufIndex++;
			pVertexArray[lReadBufIndex/3].mData[2] = lReadBuf[lReadBufIndex]; lReadBufIndex++;
		}
	}

	delete [] lReadBuf;

}

void FBXLoader::ComputeShapeDeformation(FbxMesh* pMesh, FbxTime& pTime, FbxAnimLayer * pAnimLayer, FbxVector4* pVertexArray)
{


	int lVertexCount = pMesh->GetControlPointsCount();

	FbxVector4* lSrcVertexArray = pVertexArray;
	FbxVector4* lDstVertexArray = new FbxVector4[lVertexCount];
	memcpy(lDstVertexArray, pVertexArray, lVertexCount * sizeof(FbxVector4));

	int lBlendShapeDeformerCount = pMesh->GetDeformerCount(FbxDeformer::eBlendShape);
	for(int lBlendShapeIndex = 0; lBlendShapeIndex<lBlendShapeDeformerCount; ++lBlendShapeIndex)
	{
		FbxBlendShape* lBlendShape = (FbxBlendShape*)pMesh->GetDeformer(lBlendShapeIndex, FbxDeformer::eBlendShape);

		int lBlendShapeChannelCount = lBlendShape->GetBlendShapeChannelCount();
		for(int lChannelIndex = 0; lChannelIndex<lBlendShapeChannelCount; ++lChannelIndex)
		{
			FbxBlendShapeChannel* lChannel = lBlendShape->GetBlendShapeChannel(lChannelIndex);
			if(lChannel)
			{
				// Get the percentage of influence on this channel.
				FbxAnimCurve* lFCurve = pMesh->GetShapeChannel(lBlendShapeIndex, lChannelIndex, pAnimLayer);
				if (!lFCurve) continue;
				double lWeight = lFCurve->Evaluate(pTime);

				/*
				If there is only one targetShape on this channel, the influence is easy to calculate:
				influence = (targetShape - baseGeometry) * weight * 0.01
				dstGeometry = baseGeometry + influence

				But if there are more than one targetShapes on this channel, this is an in-between 
				blendshape, also called progressive morph. The calculation of influence is different.

				For example, given two in-between targets, the full weight percentage of first target
				is 50, and the full weight percentage of the second target is 100.
				When the weight percentage reach 50, the base geometry is already be fully morphed 
				to the first target shape. When the weight go over 50, it begin to morph from the 
				first target shape to the second target shape.

				To calculate influence when the weight percentage is 25:
				1. 25 falls in the scope of 0 and 50, the morphing is from base geometry to the first target.
				2. And since 25 is already half way between 0 and 50, so the real weight percentage change to 
				the first target is 50.
				influence = (firstTargetShape - baseGeometry) * (25-0)/(50-0) * 100
				dstGeometry = baseGeometry + influence

				To calculate influence when the weight percentage is 75:
				1. 75 falls in the scope of 50 and 100, the morphing is from the first target to the second.
				2. And since 75 is already half way between 50 and 100, so the real weight percentage change 
				to the second target is 50.
				influence = (secondTargetShape - firstTargetShape) * (75-50)/(100-50) * 100
				dstGeometry = firstTargetShape + influence
				*/

				// Find the two shape indices for influence calculation according to the weight.
				// Consider index of base geometry as -1.

				int lShapeCount = lChannel->GetTargetShapeCount();
				double* lFullWeights = lChannel->GetTargetShapeFullWeights();

				// Find out which scope the lWeight falls in.
				int lStartIndex = -1;
				int lEndIndex = -1;
				for(int lShapeIndex = 0; lShapeIndex<lShapeCount; ++lShapeIndex)
				{
					if(lWeight > 0 && lWeight <= lFullWeights[0])
					{
						lEndIndex = 0;
						break;
					}
					if(lWeight > lFullWeights[lShapeIndex] && lWeight < lFullWeights[lShapeIndex+1])
					{
						lStartIndex = lShapeIndex;
						lEndIndex = lShapeIndex + 1;
						break;
					}
				}

				FbxShape* lStartShape = NULL;
				FbxShape* lEndShape = NULL;
				if(lStartIndex > -1)
				{
					lStartShape = lChannel->GetTargetShape(lStartIndex);
				}
				if(lEndIndex > -1)
				{
					lEndShape = lChannel->GetTargetShape(lEndIndex);
				}

				//The weight percentage falls between base geometry and the first target shape.
				if(lStartIndex == -1 && lEndShape) 
				{
					double lEndWeight = lFullWeights[0];
					// Calculate the real weight.
					lWeight = (lWeight/lEndWeight) * 100;
					// Initialize the lDstVertexArray with vertex of base geometry.
					memcpy(lDstVertexArray, lSrcVertexArray, lVertexCount * sizeof(FbxVector4));
					for (int j = 0; j < lVertexCount; j++)
					{
						// Add the influence of the shape vertex to the mesh vertex.
						FbxVector4 lInfluence = (lEndShape->GetControlPoints()[j] - lSrcVertexArray[j]) * lWeight * 0.01;
						lDstVertexArray[j] += lInfluence;
					}	
				}
				//The weight percentage falls between two target shapes.
				else if(lStartShape && lEndShape)
				{
					double lStartWeight = lFullWeights[lStartIndex];
					double lEndWeight = lFullWeights[lEndIndex];
					// Calculate the real weight.
					lWeight = ((lWeight-lStartWeight)/(lEndWeight-lStartWeight)) * 100;
					// Initialize the lDstVertexArray with vertex of the previous target shape geometry.
					memcpy(lDstVertexArray, lStartShape->GetControlPoints(), lVertexCount * sizeof(FbxVector4));
					for (int j = 0; j < lVertexCount; j++)
					{
						// Add the influence of the shape vertex to the previous shape vertex.
						FbxVector4 lInfluence = (lEndShape->GetControlPoints()[j] - lStartShape->GetControlPoints()[j]) * lWeight * 0.01;
						lDstVertexArray[j] += lInfluence;
					}	
				}
			}//If lChannel is valid
		}//For each blend shape channel
	}//For each blend shape deformer

	memcpy(pVertexArray, lDstVertexArray, lVertexCount * sizeof(FbxVector4));

	delete [] lDstVertexArray;

}


void FBXLoader::ComputeSkinDeformation(FbxAMatrix& pGlobalPosition, FbxMesh* pMesh,  FbxTime& pTime, FbxVector4* pVertexArray, FbxPose* pPose)
{

	FbxSkin * lSkinDeformer = (FbxSkin *)pMesh->GetDeformer(0, FbxDeformer::eSkin);
	FbxSkin::EType lSkinningType = lSkinDeformer->GetSkinningType();

	if(lSkinningType == FbxSkin::eLinear || lSkinningType == FbxSkin::eRigid)
	{
		ComputeLinearDeformation(pGlobalPosition, pMesh, pTime, pVertexArray, pPose);
	}
	else if(lSkinningType == FbxSkin::eDualQuaternion)
	{
		ComputeDualQuaternionDeformation(pGlobalPosition, pMesh, pTime, pVertexArray, pPose);
	}
	else if(lSkinningType == FbxSkin::eBlend)
	{
		int lVertexCount = pMesh->GetControlPointsCount();

		FbxVector4* lVertexArrayLinear = new FbxVector4[lVertexCount];
		memcpy(lVertexArrayLinear, pMesh->GetControlPoints(), lVertexCount * sizeof(FbxVector4));

		FbxVector4* lVertexArrayDQ = new FbxVector4[lVertexCount];
		memcpy(lVertexArrayDQ, pMesh->GetControlPoints(), lVertexCount * sizeof(FbxVector4));

		ComputeLinearDeformation(pGlobalPosition, pMesh, pTime, lVertexArrayLinear, pPose);
		ComputeDualQuaternionDeformation(pGlobalPosition, pMesh, pTime, lVertexArrayDQ, pPose);

		// To blend the skinning according to the blend weights
		// Final vertex = DQSVertex * blend weight + LinearVertex * (1- blend weight)
		// DQSVertex: vertex that is deformed by dual quaternion skinning method;
		// LinearVertex: vertex that is deformed by classic linear skinning method;
		int lBlendWeightsCount = lSkinDeformer->GetControlPointIndicesCount();
		for(int lBWIndex = 0; lBWIndex<lBlendWeightsCount; ++lBWIndex)
		{
			double lBlendWeight = lSkinDeformer->GetControlPointBlendWeights()[lBWIndex];
			pVertexArray[lBWIndex] = lVertexArrayDQ[lBWIndex] * lBlendWeight + lVertexArrayLinear[lBWIndex] * (1 - lBlendWeight);
		}
	}

}


void FBXLoader::ComputeLinearDeformation(FbxAMatrix& pGlobalPosition, FbxMesh* pMesh, FbxTime& pTime, FbxVector4* pVertexArray, FbxPose* pPose)
{

	// All the links must have the same link mode.
	FbxCluster::ELinkMode lClusterMode = ((FbxSkin*)pMesh->GetDeformer(0, FbxDeformer::eSkin))->GetCluster(0)->GetLinkMode();

	int lVertexCount = pMesh->GetControlPointsCount();
	FbxAMatrix* lClusterDeformation = new FbxAMatrix[lVertexCount];
	memset(lClusterDeformation, 0, lVertexCount * sizeof(FbxAMatrix));

	double* lClusterWeight = new double[lVertexCount];
	memset(lClusterWeight, 0, lVertexCount * sizeof(double));

	if (lClusterMode == FbxCluster::eAdditive)
	{
		for (int i = 0; i < lVertexCount; ++i)
		{
			lClusterDeformation[i].SetIdentity();
		}
	}

	// For all skins and all clusters, accumulate their deformation and weight
	// on each vertices and store them in lClusterDeformation and lClusterWeight.
	int lSkinCount = pMesh->GetDeformerCount(FbxDeformer::eSkin);
	for ( int lSkinIndex=0; lSkinIndex<lSkinCount; ++lSkinIndex)
	{
		FbxSkin * lSkinDeformer = (FbxSkin *)pMesh->GetDeformer(lSkinIndex, FbxDeformer::eSkin);

		int lClusterCount = lSkinDeformer->GetClusterCount();
		for ( int lClusterIndex=0; lClusterIndex<lClusterCount; ++lClusterIndex)
		{
			FbxCluster* lCluster = lSkinDeformer->GetCluster(lClusterIndex);
			if (!lCluster->GetLink())
				continue;

			FbxAMatrix lVertexTransformMatrix;
			ComputeClusterDeformation(pGlobalPosition, pMesh, lCluster, lVertexTransformMatrix, pTime, pPose);

			int lVertexIndexCount = lCluster->GetControlPointIndicesCount();
			for (int k = 0; k < lVertexIndexCount; ++k) 
			{            
				int lIndex = lCluster->GetControlPointIndices()[k];

				// Sometimes, the mesh can have less points than at the time of the skinning
				// because a smooth operator was active when skinning but has been deactivated during export.
				if (lIndex >= lVertexCount)
					continue;

				double lWeight = lCluster->GetControlPointWeights()[k];

				if (lWeight == 0.0)
				{
					continue;
				}

				// Compute the influence of the link on the vertex.
				FbxAMatrix lInfluence = lVertexTransformMatrix;
				MatrixScale(lInfluence, lWeight);

				if (lClusterMode == FbxCluster::eAdditive)
				{    
					// Multiply with the product of the deformations on the vertex.
					MatrixAddToDiagonal(lInfluence, 1.0 - lWeight);
					lClusterDeformation[lIndex] = lInfluence * lClusterDeformation[lIndex];

					// Set the link to 1.0 just to know this vertex is influenced by a link.
					lClusterWeight[lIndex] = 1.0;
				}
				else // lLinkMode == FbxCluster::eNormalize || lLinkMode == FbxCluster::eTotalOne
				{
					// Add to the sum of the deformations on the vertex.
					MatrixAdd(lClusterDeformation[lIndex], lInfluence);

					// Add to the sum of weights to either normalize or complete the vertex.
					lClusterWeight[lIndex] += lWeight;
				}
			}//For each vertex			
		}//lClusterCount
	}

	//Actually deform each vertices here by information stored in lClusterDeformation and lClusterWeight
	for (int i = 0; i < lVertexCount; i++) 
	{
		FbxVector4 lSrcVertex = pVertexArray[i];
		FbxVector4& lDstVertex = pVertexArray[i];
		double lWeight = lClusterWeight[i];

		// Deform the vertex if there was at least a link with an influence on the vertex,
		if (lWeight != 0.0) 
		{
			lDstVertex = lClusterDeformation[i].MultT(lSrcVertex);
			if (lClusterMode == FbxCluster::eNormalize)
			{
				// In the normalized link mode, a vertex is always totally influenced by the links. 
				lDstVertex /= lWeight;
			}
			else if (lClusterMode == FbxCluster::eTotalOne)
			{
				// In the total 1 link mode, a vertex can be partially influenced by the links. 
				lSrcVertex *= (1.0 - lWeight);
				lDstVertex += lSrcVertex;
			}
		} 
	}

	delete [] lClusterDeformation;
	delete [] lClusterWeight;

}


void FBXLoader::ComputeClusterDeformation(FbxAMatrix& pGlobalPosition, 
										  FbxMesh* pMesh,FbxCluster* pCluster, FbxAMatrix& pVertexTransformMatrix,FbxTime pTime, FbxPose* pPose)
{

	FbxCluster::ELinkMode lClusterMode = pCluster->GetLinkMode();

	FbxAMatrix lReferenceGlobalInitPosition;
	FbxAMatrix lReferenceGlobalCurrentPosition;
	FbxAMatrix lAssociateGlobalInitPosition;
	FbxAMatrix lAssociateGlobalCurrentPosition;
	FbxAMatrix lClusterGlobalInitPosition;
	FbxAMatrix lClusterGlobalCurrentPosition;

	FbxAMatrix lReferenceGeometry;
	FbxAMatrix lAssociateGeometry;
	FbxAMatrix lClusterGeometry;

	FbxAMatrix lClusterRelativeInitPosition;
	FbxAMatrix lClusterRelativeCurrentPositionInverse;

	if (lClusterMode == FbxCluster::eAdditive && pCluster->GetAssociateModel())
	{
		pCluster->GetTransformAssociateModelMatrix(lAssociateGlobalInitPosition);
		// Geometric transform of the model
		lAssociateGeometry = GetGeometry(pCluster->GetAssociateModel());
		lAssociateGlobalInitPosition *= lAssociateGeometry;
		lAssociateGlobalCurrentPosition = GetGlobalPosition(pCluster->GetAssociateModel(), pTime, pPose);

		pCluster->GetTransformMatrix(lReferenceGlobalInitPosition);
		// Multiply lReferenceGlobalInitPosition by Geometric Transformation
		lReferenceGeometry = GetGeometry(pMesh->GetNode());
		lReferenceGlobalInitPosition *= lReferenceGeometry;
		lReferenceGlobalCurrentPosition = pGlobalPosition;

		// Get the link initial global position and the link current global position.
		pCluster->GetTransformLinkMatrix(lClusterGlobalInitPosition);
		// Multiply lClusterGlobalInitPosition by Geometric Transformation
		lClusterGeometry = GetGeometry(pCluster->GetLink());
		lClusterGlobalInitPosition *= lClusterGeometry;
		lClusterGlobalCurrentPosition = GetGlobalPosition(pCluster->GetLink(), pTime, pPose);

		// Compute the shift of the link relative to the reference.
		//ModelM-1 * AssoM * AssoGX-1 * LinkGX * LinkM-1*ModelM
		pVertexTransformMatrix = lReferenceGlobalInitPosition.Inverse() * lAssociateGlobalInitPosition * lAssociateGlobalCurrentPosition.Inverse() *
			lClusterGlobalCurrentPosition * lClusterGlobalInitPosition.Inverse() * lReferenceGlobalInitPosition;
	}
	else
	{
		pCluster->GetTransformMatrix(lReferenceGlobalInitPosition);
		lReferenceGlobalCurrentPosition = pGlobalPosition;
		// Multiply lReferenceGlobalInitPosition by Geometric Transformation
		lReferenceGeometry = GetGeometry(pMesh->GetNode());
		lReferenceGlobalInitPosition *= lReferenceGeometry;

		// Get the link initial global position and the link current global position.
		pCluster->GetTransformLinkMatrix(lClusterGlobalInitPosition);
		lClusterGlobalCurrentPosition = GetGlobalPosition(pCluster->GetLink(), pTime, pPose);

		// Compute the initial position of the link relative to the reference.
		lClusterRelativeInitPosition = lClusterGlobalInitPosition.Inverse() * lReferenceGlobalInitPosition;

		// Compute the current position of the link relative to the reference.
		lClusterRelativeCurrentPositionInverse = lReferenceGlobalCurrentPosition.Inverse() * lClusterGlobalCurrentPosition;

		// Compute the shift of the link relative to the reference.
		pVertexTransformMatrix = lClusterRelativeCurrentPositionInverse * lClusterRelativeInitPosition;
	}

}



void FBXLoader::MatrixScale(FbxAMatrix& pMatrix, double pValue)
{

	int i,j;

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			pMatrix[i][j] *= pValue;
		}
	}

}

void FBXLoader::MatrixAddToDiagonal(FbxAMatrix& pMatrix, double pValue)
{
	pMatrix[0][0] += pValue;
	pMatrix[1][1] += pValue;
	pMatrix[2][2] += pValue;
	pMatrix[3][3] += pValue;
}


void FBXLoader::MatrixAdd(FbxAMatrix& pDstMatrix, FbxAMatrix& pSrcMatrix)
{

	int i,j;

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			pDstMatrix[i][j] += pSrcMatrix[i][j];
		}
	}

}


void FBXLoader::ComputeDualQuaternionDeformation(FbxAMatrix& pGlobalPosition, 
												 FbxMesh* pMesh, FbxTime& pTime,FbxVector4* pVertexArray,FbxPose* pPose)
{

	// All the links must have the same link mode.
	FbxCluster::ELinkMode lClusterMode = ((FbxSkin*)pMesh->GetDeformer(0, FbxDeformer::eSkin))->GetCluster(0)->GetLinkMode();

	int lVertexCount = pMesh->GetControlPointsCount();
	int lSkinCount = pMesh->GetDeformerCount(FbxDeformer::eSkin);

	FbxDualQuaternion* lDQClusterDeformation = new FbxDualQuaternion[lVertexCount];
	memset(lDQClusterDeformation, 0, lVertexCount * sizeof(FbxDualQuaternion));

	double* lClusterWeight = new double[lVertexCount];
	memset(lClusterWeight, 0, lVertexCount * sizeof(double));

	// For all skins and all clusters, accumulate their deformation and weight
	// on each vertices and store them in lClusterDeformation and lClusterWeight.
	for ( int lSkinIndex=0; lSkinIndex<lSkinCount; ++lSkinIndex)
	{
		FbxSkin * lSkinDeformer = (FbxSkin *)pMesh->GetDeformer(lSkinIndex, FbxDeformer::eSkin);
		int lClusterCount = lSkinDeformer->GetClusterCount();
		for ( int lClusterIndex=0; lClusterIndex<lClusterCount; ++lClusterIndex)
		{
			FbxCluster* lCluster = lSkinDeformer->GetCluster(lClusterIndex);
			if (!lCluster->GetLink())
				continue;

			FbxAMatrix lVertexTransformMatrix;
			ComputeClusterDeformation(pGlobalPosition, pMesh, lCluster, lVertexTransformMatrix, pTime, pPose);

			FbxQuaternion lQ = lVertexTransformMatrix.GetQ();
			FbxVector4 lT = lVertexTransformMatrix.GetT();
			FbxDualQuaternion lDualQuaternion(lQ, lT);

			int lVertexIndexCount = lCluster->GetControlPointIndicesCount();
			for (int k = 0; k < lVertexIndexCount; ++k) 
			{ 
				int lIndex = lCluster->GetControlPointIndices()[k];

				// Sometimes, the mesh can have less points than at the time of the skinning
				// because a smooth operator was active when skinning but has been deactivated during export.
				if (lIndex >= lVertexCount)
					continue;

				double lWeight = lCluster->GetControlPointWeights()[k];

				if (lWeight == 0.0)
					continue;

				// Compute the influence of the link on the vertex.
				FbxDualQuaternion lInfluence = lDualQuaternion * lWeight;
				if (lClusterMode == FbxCluster::eAdditive)
				{    
					// Simply influenced by the dual quaternion.
					lDQClusterDeformation[lIndex] = lInfluence;

					// Set the link to 1.0 just to know this vertex is influenced by a link.
					lClusterWeight[lIndex] = 1.0;
				}
				else // lLinkMode == FbxCluster::eNormalize || lLinkMode == FbxCluster::eTotalOne
				{
					if(lClusterIndex == 0)
					{
						lDQClusterDeformation[lIndex] = lInfluence;
					}
					else
					{
						// Add to the sum of the deformations on the vertex.
						// Make sure the deformation is accumulated in the same rotation direction. 
						// Use dot product to judge the sign.
						double lSign = lDQClusterDeformation[lIndex].GetFirstQuaternion().DotProduct(lDualQuaternion.GetFirstQuaternion());
						if( lSign >= 0.0 )
						{
							lDQClusterDeformation[lIndex] += lInfluence;
						}
						else
						{
							lDQClusterDeformation[lIndex] -= lInfluence;
						}
					}
					// Add to the sum of weights to either normalize or complete the vertex.
					lClusterWeight[lIndex] += lWeight;
				}
			}//For each vertex
		}//lClusterCount
	}

	//Actually deform each vertices here by information stored in lClusterDeformation and lClusterWeight
	for (int i = 0; i < lVertexCount; i++) 
	{
		FbxVector4 lSrcVertex = pVertexArray[i];
		FbxVector4& lDstVertex = pVertexArray[i];
		double lWeightSum = lClusterWeight[i];

		// Deform the vertex if there was at least a link with an influence on the vertex,
		if (lWeightSum != 0.0) 
		{
			lDQClusterDeformation[i].Normalize();
			lDstVertex = lDQClusterDeformation[i].Deform(lDstVertex);

			if (lClusterMode == FbxCluster::eNormalize)
			{
				// In the normalized link mode, a vertex is always totally influenced by the links. 
				lDstVertex /= lWeightSum;
			}
			else if (lClusterMode == FbxCluster::eTotalOne)
			{
				// In the total 1 link mode, a vertex can be partially influenced by the links. 
				lSrcVertex *= (1.0 - lWeightSum);
				lDstVertex += lSrcVertex;
			}
		} 
	}

	delete [] lDQClusterDeformation;
	delete [] lClusterWeight;

}



void FBXLoader::SetSelectedNode(FbxNode * pSelectedNode)
{
	SelectedNode = pSelectedNode;
}

void FBXLoader::InitializeSdkObjects(FbxManager*& pManager, FbxScene*& pScene)
{

	//The first thing to do is to create the FBX Manager which is the object allocator for almost all the classes in the SDK
	pManager = FbxManager::Create();
	if( !pManager )
	{
		FBXSDK_printf("Error: Unable to create FBX Manager!\n");
		exit(1);
	}
	else FBXSDK_printf("Autodesk FBX SDK version %s\n", pManager->GetVersion());

	//Create an IOSettings object. This object holds all import/export settings.
	FbxIOSettings* ios = FbxIOSettings::Create(pManager, IOSROOT);
	pManager->SetIOSettings(ios);

	//Load plugins from the executable directory (optional)
	FbxString lPath = FbxGetApplicationDirectory();
	pManager->LoadPluginsDirectory(lPath.Buffer());

	//Create an FBX scene. This object holds most objects imported/exported from/to files.
	pScene = FbxScene::Create(pManager, "My Scene");
	if( !pScene )
	{
		FBXSDK_printf("Error: Unable to create FBX scene!\n");
		exit(1);
	}


}


void FBXLoader::DestroySdkObjects(FbxManager* pManager, bool pExitStatus)
{
	//Delete the FBX Manager. All the objects that have been allocated using the FBX Manager and that haven't been explicitly destroyed are also automatically destroyed.
	if( pManager ) pManager->Destroy();
	if( pExitStatus ) FBXSDK_printf("Program Success!\n");
}


void FBXLoader::DisplayGrid(const FbxAMatrix & pTransform)
{

	glPushMatrix();
	glMultMatrixd(pTransform);

	// Draw a grid 500*500
	glColor3f(0.3f, 0.3f, 0.3f);
	glLineWidth(1.0);
	const int hw = 500;
	const int step = 20;
	const int bigstep = 100;
	int       i;

	// Draw Grid
	for (i = -hw; i <= hw; i+=step) {

		if (i % bigstep == 0) {
			glLineWidth(2.0);
		} else {
			glLineWidth(1.0);
		}
		glBegin(GL_LINES);
		glVertex3i(i,0,-hw);
		glVertex3i(i,0,hw);
		glEnd();
		glBegin(GL_LINES);
		glVertex3i(-hw,0,i);
		glVertex3i(hw,0,i);
		glEnd();

	}
	glPopMatrix();

}  //optional



///////////////////////////////////////////////////////////VBO_MESH/////////////////////////////////

VBOMesh::VBOMesh() : mHasNormal(false), mHasUV(false), mAllByControlPoint(true)
{
	// Reset every VBO to zero, which means no buffer.
	for (int lVBOIndex = 0; lVBOIndex < VBO_COUNT; ++lVBOIndex)
	{
		mVBONames[lVBOIndex] = 0;
	}
}

VBOMesh::~VBOMesh()
{
	// Delete VBO objects, zeros are ignored automatically.
	glDeleteBuffers(VBO_COUNT, mVBONames);

	//	FbxArrayDelete(mSubMeshes);

	for(int i=0; i < mSubMeshes.GetCount(); i++)
	{
		delete mSubMeshes[i];
	}

	mSubMeshes.Clear();

}

bool VBOMesh::Initialize(const FbxMesh *pMesh)
{
	if (!pMesh->GetNode())
		return false;

	const int lPolygonCount = pMesh->GetPolygonCount();

	// Count the polygon count of each material
	FbxLayerElementArrayTemplate<int>* lMaterialIndice = NULL;
	FbxGeometryElement::EMappingMode lMaterialMappingMode = FbxGeometryElement::eNone;
	if (pMesh->GetElementMaterial())
	{
		lMaterialIndice = &pMesh->GetElementMaterial()->GetIndexArray();
		lMaterialMappingMode = pMesh->GetElementMaterial()->GetMappingMode();
		if (lMaterialIndice && lMaterialMappingMode == FbxGeometryElement::eByPolygon)
		{
			FBX_ASSERT(lMaterialIndice->GetCount() == lPolygonCount);
			if (lMaterialIndice->GetCount() == lPolygonCount)
			{
				// Count the faces of each material
				for (int lPolygonIndex = 0; lPolygonIndex < lPolygonCount; ++lPolygonIndex)
				{
					const int lMaterialIndex = lMaterialIndice->GetAt(lPolygonIndex);
					if (mSubMeshes.GetCount() < lMaterialIndex + 1)
					{
						mSubMeshes.Resize(lMaterialIndex + 1);
					}
					if (mSubMeshes[lMaterialIndex] == NULL)
					{
						mSubMeshes[lMaterialIndex] = new SubMesh;
					}
					mSubMeshes[lMaterialIndex]->TriangleCount += 1;
				}

				// Make sure we have no "holes" (NULL) in the mSubMeshes table. This can happen
				// if, in the loop above, we resized the mSubMeshes by more than one slot.
				for (int i = 0; i < mSubMeshes.GetCount(); i++)
				{
					if (mSubMeshes[i] == NULL)
						mSubMeshes[i] = new SubMesh;
				}

				// Record the offset (how many vertex)
				const int lMaterialCount = mSubMeshes.GetCount();
				int lOffset = 0;
				for (int lIndex = 0; lIndex < lMaterialCount; ++lIndex)
				{
					mSubMeshes[lIndex]->IndexOffset = lOffset;
					lOffset += mSubMeshes[lIndex]->TriangleCount * 3;
					// This will be used as counter in the following procedures, reset to zero
					mSubMeshes[lIndex]->TriangleCount = 0;
				}
				FBX_ASSERT(lOffset == lPolygonCount * 3);
			}
		}
	}

	// All faces will use the same material.
	if (mSubMeshes.GetCount() == 0)
	{
		mSubMeshes.Resize(1);
		mSubMeshes[0] = new SubMesh();
	}

	// Congregate all the data of a mesh to be cached in VBOs.
	// If normal or UV is by polygon vertex, record all vertex attributes by polygon vertex.
	mHasNormal = pMesh->GetElementNormalCount() > 0;
	mHasUV = pMesh->GetElementUVCount() > 0;
	FbxGeometryElement::EMappingMode lNormalMappingMode = FbxGeometryElement::eNone;
	FbxGeometryElement::EMappingMode lUVMappingMode = FbxGeometryElement::eNone;
	if (mHasNormal)
	{
		lNormalMappingMode = pMesh->GetElementNormal(0)->GetMappingMode();
		if (lNormalMappingMode == FbxGeometryElement::eNone)
		{
			mHasNormal = false;
		}
		if (mHasNormal && lNormalMappingMode != FbxGeometryElement::eByControlPoint)
		{
			mAllByControlPoint = false;
		}
	}
	if (mHasUV)
	{
		lUVMappingMode = pMesh->GetElementUV(0)->GetMappingMode();
		if (lUVMappingMode == FbxGeometryElement::eNone)
		{
			mHasUV = false;
		}
		if (mHasUV && lUVMappingMode != FbxGeometryElement::eByControlPoint)
		{
			mAllByControlPoint = false;
		}
	}

	// Allocate the array memory, by control point or by polygon vertex.
	int lPolygonVertexCount = pMesh->GetControlPointsCount();
	if (!mAllByControlPoint)
	{
		lPolygonVertexCount = lPolygonCount * TRIANGLE_VERTEX_COUNT;
	}
	float * lVertices = new float[lPolygonVertexCount * VERTEX_STRIDE];
	unsigned int * lIndices = new unsigned int[lPolygonCount * TRIANGLE_VERTEX_COUNT];
	float * lNormals = NULL;
	if (mHasNormal)
	{
		lNormals = new float[lPolygonVertexCount * NORMAL_STRIDE];
	}
	float * lUVs = NULL;
	FbxStringList lUVNames;
	pMesh->GetUVSetNames(lUVNames);
	const char * lUVName = NULL;
	if (mHasUV && lUVNames.GetCount())
	{
		lUVs = new float[lPolygonVertexCount * UV_STRIDE];
		lUVName = lUVNames[0];
	}

	// Populate the array with vertex attribute, if by control point.
	const FbxVector4 * lControlPoints = pMesh->GetControlPoints();
	FbxVector4 lCurrentVertex;
	FbxVector4 lCurrentNormal;
	FbxVector2 lCurrentUV;
	if (mAllByControlPoint)
	{
		const FbxGeometryElementNormal * lNormalElement = NULL;
		const FbxGeometryElementUV * lUVElement = NULL;
		if (mHasNormal)
		{
			lNormalElement = pMesh->GetElementNormal(0);
		}
		if (mHasUV)
		{
			lUVElement = pMesh->GetElementUV(0);
		}
		for (int lIndex = 0; lIndex < lPolygonVertexCount; ++lIndex)
		{
			// Save the vertex position.
			lCurrentVertex = lControlPoints[lIndex];
			lVertices[lIndex * VERTEX_STRIDE] = static_cast<float>(lCurrentVertex[0]);
			lVertices[lIndex * VERTEX_STRIDE + 1] = static_cast<float>(lCurrentVertex[1]);
			lVertices[lIndex * VERTEX_STRIDE + 2] = static_cast<float>(lCurrentVertex[2]);
			lVertices[lIndex * VERTEX_STRIDE + 3] = 1;

			// Save the normal.
			if (mHasNormal)
			{
				int lNormalIndex = lIndex;
				if (lNormalElement->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
				{
					lNormalIndex = lNormalElement->GetIndexArray().GetAt(lIndex);
				}
				lCurrentNormal = lNormalElement->GetDirectArray().GetAt(lNormalIndex);
				lNormals[lIndex * NORMAL_STRIDE] = static_cast<float>(lCurrentNormal[0]);
				lNormals[lIndex * NORMAL_STRIDE + 1] = static_cast<float>(lCurrentNormal[1]);
				lNormals[lIndex * NORMAL_STRIDE + 2] = static_cast<float>(lCurrentNormal[2]);
			}

			// Save the UV.
			if (mHasUV)
			{
				int lUVIndex = lIndex;
				if (lUVElement->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
				{
					lUVIndex = lUVElement->GetIndexArray().GetAt(lIndex);
				}
				lCurrentUV = lUVElement->GetDirectArray().GetAt(lUVIndex);
				lUVs[lIndex * UV_STRIDE] = static_cast<float>(lCurrentUV[0]);
				lUVs[lIndex * UV_STRIDE + 1] = static_cast<float>(lCurrentUV[1]);
			}
		}

	}

	int lVertexCount = 0;
	for (int lPolygonIndex = 0; lPolygonIndex < lPolygonCount; ++lPolygonIndex)
	{
		// The material for current face.
		int lMaterialIndex = 0;
		if (lMaterialIndice && lMaterialMappingMode == FbxGeometryElement::eByPolygon)
		{
			lMaterialIndex = lMaterialIndice->GetAt(lPolygonIndex);
		}

		// Where should I save the vertex attribute index, according to the material
		const int lIndexOffset = mSubMeshes[lMaterialIndex]->IndexOffset +
			mSubMeshes[lMaterialIndex]->TriangleCount * 3;
		for (int lVerticeIndex = 0; lVerticeIndex < TRIANGLE_VERTEX_COUNT; ++lVerticeIndex)
		{
			const int lControlPointIndex = pMesh->GetPolygonVertex(lPolygonIndex, lVerticeIndex);

			if (mAllByControlPoint)
			{
				lIndices[lIndexOffset + lVerticeIndex] = static_cast<unsigned int>(lControlPointIndex);
			}
			// Populate the array with vertex attribute, if by polygon vertex.
			else
			{
				lIndices[lIndexOffset + lVerticeIndex] = static_cast<unsigned int>(lVertexCount);

				lCurrentVertex = lControlPoints[lControlPointIndex];
				lVertices[lVertexCount * VERTEX_STRIDE] = static_cast<float>(lCurrentVertex[0]);
				lVertices[lVertexCount * VERTEX_STRIDE + 1] = static_cast<float>(lCurrentVertex[1]);
				lVertices[lVertexCount * VERTEX_STRIDE + 2] = static_cast<float>(lCurrentVertex[2]);
				lVertices[lVertexCount * VERTEX_STRIDE + 3] = 1;

				if (mHasNormal)
				{
					pMesh->GetPolygonVertexNormal(lPolygonIndex, lVerticeIndex, lCurrentNormal);
					lNormals[lVertexCount * NORMAL_STRIDE] = static_cast<float>(lCurrentNormal[0]);
					lNormals[lVertexCount * NORMAL_STRIDE + 1] = static_cast<float>(lCurrentNormal[1]);
					lNormals[lVertexCount * NORMAL_STRIDE + 2] = static_cast<float>(lCurrentNormal[2]);
				}

				if (mHasUV)
				{
					bool lUnmappedUV;
					pMesh->GetPolygonVertexUV(lPolygonIndex, lVerticeIndex, lUVName, lCurrentUV, lUnmappedUV);
					lUVs[lVertexCount * UV_STRIDE] = static_cast<float>(lCurrentUV[0]);
					lUVs[lVertexCount * UV_STRIDE + 1] = static_cast<float>(lCurrentUV[1]);
				}
			}
			++lVertexCount;
		}
		mSubMeshes[lMaterialIndex]->TriangleCount += 1;
	}

	// Create VBOs
	glGenBuffers(VBO_COUNT, mVBONames);

	// Save vertex attributes into GPU
	glBindBuffer(GL_ARRAY_BUFFER, mVBONames[VERTEX_VBO]);
	glBufferData(GL_ARRAY_BUFFER, lPolygonVertexCount * VERTEX_STRIDE * sizeof(float), lVertices, GL_STATIC_DRAW);
	delete [] lVertices;

	if (mHasNormal)
	{
		glBindBuffer(GL_ARRAY_BUFFER, mVBONames[NORMAL_VBO]);
		glBufferData(GL_ARRAY_BUFFER, lPolygonVertexCount * NORMAL_STRIDE * sizeof(float), lNormals, GL_STATIC_DRAW);
		delete [] lNormals;
	}

	if (mHasUV)
	{
		glBindBuffer(GL_ARRAY_BUFFER, mVBONames[UV_VBO]);
		glBufferData(GL_ARRAY_BUFFER, lPolygonVertexCount * UV_STRIDE * sizeof(float), lUVs, GL_STATIC_DRAW);
		delete [] lUVs;
	}

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mVBONames[INDEX_VBO]);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, lPolygonCount * TRIANGLE_VERTEX_COUNT * sizeof(unsigned int), lIndices, GL_STATIC_DRAW);
	delete [] lIndices;

	return true;
}

void VBOMesh::UpdateVertexPosition(const FbxMesh * pMesh, const FbxVector4 * pVertices) const
{
	// Convert to the same sequence with data in GPU.
	float * lVertices = NULL;
	int lVertexCount = 0;
	if (mAllByControlPoint)
	{
		lVertexCount = pMesh->GetControlPointsCount();
		lVertices = new float[lVertexCount * VERTEX_STRIDE];
		for (int lIndex = 0; lIndex < lVertexCount; ++lIndex)
		{
			lVertices[lIndex * VERTEX_STRIDE] = static_cast<float>(pVertices[lIndex][0]);
			lVertices[lIndex * VERTEX_STRIDE + 1] = static_cast<float>(pVertices[lIndex][1]);
			lVertices[lIndex * VERTEX_STRIDE + 2] = static_cast<float>(pVertices[lIndex][2]);
			lVertices[lIndex * VERTEX_STRIDE + 3] = 1;
		}
	}
	else
	{
		const int lPolygonCount = pMesh->GetPolygonCount();
		lVertexCount = lPolygonCount * TRIANGLE_VERTEX_COUNT;
		lVertices = new float[lVertexCount * VERTEX_STRIDE];

		int lVertexCount = 0;
		for (int lPolygonIndex = 0; lPolygonIndex < lPolygonCount; ++lPolygonIndex)
		{
			for (int lVerticeIndex = 0; lVerticeIndex < TRIANGLE_VERTEX_COUNT; ++lVerticeIndex)
			{
				const int lControlPointIndex = pMesh->GetPolygonVertex(lPolygonIndex, lVerticeIndex);
				lVertices[lVertexCount * VERTEX_STRIDE] = static_cast<float>(pVertices[lControlPointIndex][0]);
				lVertices[lVertexCount * VERTEX_STRIDE + 1] = static_cast<float>(pVertices[lControlPointIndex][1]);
				lVertices[lVertexCount * VERTEX_STRIDE + 2] = static_cast<float>(pVertices[lControlPointIndex][2]);
				lVertices[lVertexCount * VERTEX_STRIDE + 3] = 1;
				++lVertexCount;
			}
		}
	}

	// Transfer into GPU.
	if (lVertices)
	{
		glBindBuffer(GL_ARRAY_BUFFER, mVBONames[VERTEX_VBO]);
		glBufferData(GL_ARRAY_BUFFER, lVertexCount * VERTEX_STRIDE * sizeof(float), lVertices, GL_STATIC_DRAW);
		delete [] lVertices;
	}
}

void VBOMesh::Draw(int pMaterialIndex) const
{
	// Where to start.
	GLsizei lOffset = mSubMeshes[pMaterialIndex]->IndexOffset * sizeof(unsigned int);
	// if ( pShadingMode == SHADING_MODE_SHADED)
	// {
	const GLsizei lElementCount = mSubMeshes[pMaterialIndex]->TriangleCount * 3;
	glDrawElements(GL_TRIANGLES, lElementCount, GL_UNSIGNED_INT, reinterpret_cast<const GLvoid *>(lOffset));
	// }
	//else
	//{
	//    for (int lIndex = 0; lIndex < mSubMeshes[pMaterialIndex]->TriangleCount; ++lIndex)
	//    {
	//        // Draw line loop for every triangle.
	//        glDrawElements(GL_LINE_LOOP, TRIANGLE_VERTEX_COUNT, GL_UNSIGNED_INT, reinterpret_cast<const GLvoid *>(lOffset));
	//        lOffset += sizeof(unsigned int) * TRIANGLE_VERTEX_COUNT;
	//    }
	//}
}

void VBOMesh::BeginDraw() const
{
	// Push OpenGL attributes.
	glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
	glPushAttrib(GL_ENABLE_BIT);
	glPushAttrib(GL_CURRENT_BIT);
	glPushAttrib(GL_LIGHTING_BIT);
	glPushAttrib(GL_TEXTURE_BIT);

	// Set vertex position array.
	glBindBuffer(GL_ARRAY_BUFFER, mVBONames[VERTEX_VBO]);
	glVertexPointer(VERTEX_STRIDE, GL_FLOAT, 0, 0);
	glEnableClientState(GL_VERTEX_ARRAY);

	// Set normal array.
	if (mHasNormal)
	{
		glBindBuffer(GL_ARRAY_BUFFER, mVBONames[NORMAL_VBO]);
		glNormalPointer(GL_FLOAT, 0, 0);
		glEnableClientState(GL_NORMAL_ARRAY);
	}

	// Set UV array.
	if (mHasUV)
	{
		glBindBuffer(GL_ARRAY_BUFFER, mVBONames[UV_VBO]);
		glTexCoordPointer(UV_STRIDE, GL_FLOAT, 0, 0);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	// Set index array.
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mVBONames[INDEX_VBO]);


	glEnable(GL_LIGHTING);

	glEnable(GL_TEXTURE_2D);

	glEnable(GL_NORMALIZE);

}

void VBOMesh::EndDraw() const
{
	// Reset VBO binding.
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	// Pop OpenGL attributes.
	glPopAttrib();
	glPopAttrib();
	glPopAttrib();
	glPopAttrib();
	glPopClientAttrib();
}


////////////////////////////////////////////////////////////matrial /////////////////////////////////

MaterialCache::MaterialCache() : mShinness(0)
{

}

MaterialCache::~MaterialCache()
{

}

// Bake material properties.
bool MaterialCache::Initialize(const FbxSurfaceMaterial * pMaterial)
{
	const FbxDouble3 lEmissive = GetMaterialProperty(pMaterial,
		FbxSurfaceMaterial::sEmissive, FbxSurfaceMaterial::sEmissiveFactor, mEmissive.mTextureName);
	mEmissive.mColor[0] = static_cast<GLfloat>(lEmissive[0]);
	mEmissive.mColor[1] = static_cast<GLfloat>(lEmissive[1]);
	mEmissive.mColor[2] = static_cast<GLfloat>(lEmissive[2]);

	const FbxDouble3 lAmbient = GetMaterialProperty(pMaterial,
		FbxSurfaceMaterial::sAmbient, FbxSurfaceMaterial::sAmbientFactor, mAmbient.mTextureName);
	mAmbient.mColor[0] = static_cast<GLfloat>(lAmbient[0]);
	mAmbient.mColor[1] = static_cast<GLfloat>(lAmbient[1]);
	mAmbient.mColor[2] = static_cast<GLfloat>(lAmbient[2]);

	const FbxDouble3 lDiffuse = GetMaterialProperty(pMaterial,
		FbxSurfaceMaterial::sDiffuse, FbxSurfaceMaterial::sDiffuseFactor, mDiffuse.mTextureName);
	mDiffuse.mColor[0] = static_cast<GLfloat>(lDiffuse[0]);
	mDiffuse.mColor[1] = static_cast<GLfloat>(lDiffuse[1]);
	mDiffuse.mColor[2] = static_cast<GLfloat>(lDiffuse[2]);

	const FbxDouble3 lSpecular = GetMaterialProperty(pMaterial,
		FbxSurfaceMaterial::sSpecular, FbxSurfaceMaterial::sSpecularFactor, mSpecular.mTextureName);
	mSpecular.mColor[0] = static_cast<GLfloat>(lSpecular[0]);
	mSpecular.mColor[1] = static_cast<GLfloat>(lSpecular[1]);
	mSpecular.mColor[2] = static_cast<GLfloat>(lSpecular[2]);

	FbxProperty lShininessProperty = pMaterial->FindProperty(FbxSurfaceMaterial::sShininess);
	if (lShininessProperty.IsValid())
	{
		double lShininess = lShininessProperty.Get<FbxDouble>();
		mShinness = static_cast<GLfloat>(lShininess);
	}

	return true;
}

void MaterialCache::SetCurrentMaterial() const
{
	glMaterialfv(GL_FRONT, GL_EMISSION, mEmissive.mColor);
	glMaterialfv(GL_FRONT, GL_AMBIENT, mAmbient.mColor);
	glMaterialfv(GL_FRONT, GL_DIFFUSE, mDiffuse.mColor);
	glMaterialfv(GL_FRONT, GL_SPECULAR, mSpecular.mColor);
	glMaterialf(GL_FRONT, GL_SHININESS, mShinness);

	glBindTexture(GL_TEXTURE_2D, mDiffuse.mTextureName);
}

void MaterialCache::SetDefaultMaterial()
{
	const GLfloat BLACK_COLOR[] = {0.0f, 0.0f, 0.0f, 1.0f};
	const GLfloat GREEN_COLOR[] = {0.0f, 1.0f, 0.0f, 1.0f};
	glMaterialfv(GL_FRONT, GL_EMISSION, BLACK_COLOR);
	glMaterialfv(GL_FRONT, GL_AMBIENT, BLACK_COLOR);
	glMaterialfv(GL_FRONT, GL_DIFFUSE, GREEN_COLOR);
	glMaterialfv(GL_FRONT, GL_SPECULAR, BLACK_COLOR);
	glMaterialf(GL_FRONT, GL_SHININESS, 0);

	glBindTexture(GL_TEXTURE_2D, 0);
}
