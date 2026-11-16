#include "EnginePCH.h"
#include "Editor/ContentDrawer/MeshThumbnailRenderer.h"

#include "Animation/AnimBlueprintData.h"
#include "Animation/AnimBlueprintFile.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/BlendSpaceFile.h"
#include "Animation/BonePose.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/Misc/Paths.h"
#include "Editor/AssetEditor/FPreviewScene.h"
#include "Editor/KurisuEd/EditorEngine.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/BoxSphereBounds.h"
#include "Render/Mesh.h"
#include "Render/RenderCommand.h"
#include "Render/Renderer.h"
#include "Render/RenderResourceManager.h"
#include "Render/SceneRenderer.h"
#include "Render/SkeletalMesh.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
	std::string ToLowerAscii(std::string Text)
	{
		std::transform(Text.begin(), Text.end(), Text.begin(),
			[](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
		return Text;
	}

	bool IsObjExtension(const fs::path& FilePath)
	{
		return ToLowerAscii(FilePath.extension().generic_string()) == ".obj";
	}

	bool AreBoundsFinite(const FBox& Box)
	{
		return std::isfinite(Box.Min.X) && std::isfinite(Box.Min.Y) && std::isfinite(Box.Min.Z)
			&& std::isfinite(Box.Max.X) && std::isfinite(Box.Max.Y) && std::isfinite(Box.Max.Z)
			&& Box.Min.X <= Box.Max.X && Box.Min.Y <= Box.Max.Y && Box.Min.Z <= Box.Max.Z;
	}

	URenderAsset* LoadMeshAssetForThumbnail(const fs::path& FilePath, const FString& Utf8Path)
	{
		if (URenderAsset* Loaded = UAssetManager::FindLoadedAsset(Utf8Path))
		{
			return Loaded;
		}
		if (URenderAsset* Asset = UAssetManager::GetAssetByPath<URenderAsset>(Utf8Path))
		{
			return Asset;
		}

		const FString Resolved = FAssetUtils::ResolveAssetFilePath(Utf8Path);
		const FString LoadPath = Resolved.empty() ? Utf8Path : Resolved;

		if (IsObjExtension(FilePath))
		{
			return UAssetManager::LoadObjStaticMesh(LoadPath);
		}
		if (FFbxMeshBake::IsBakedPath(LoadPath))
		{
			FString Error;
			return UAssetManager::LoadFbxMesh(LoadPath, Error);
		}
		return nullptr;
	}

	bool HasCompatibleSkeletonTrack(const UAnimSequence& Sequence, const USkeletalMesh& Mesh)
	{
		const UAnimDataModel* Model = Sequence.GetDataModel();
		if (!Model)
		{
			return false;
		}
		const TArray<FBoneAnimationTrack>& Tracks = Model->GetBoneAnimationTracks();
		if (Tracks.IsEmpty())
		{
			return false;
		}
		for (const FBoneAnimationTrack& Track : Tracks)
		{
			if (Mesh.FindBoneIndex(Track.Name) >= 0)
			{
				return true;
			}
		}
		return false;
	}

	FBox ComputePosedSkeletonBounds(const USkeletalMeshComponent& Comp, const FBox& FallbackBindBox)
	{
		const int32 NumBones = Comp.GetBones().Num();
		if (NumBones <= 0)
		{
			return FallbackBindBox;
		}

		FBox PoseBox(
			FVector(FLT_MAX, FLT_MAX, FLT_MAX),
			FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX));
		bool bAnyBone = false;
		for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
		{
			const FVector4 Pos = Comp.GetBoneComponentMatrix(BoneIdx).GetOrigin();
			if (!std::isfinite(Pos.X) || !std::isfinite(Pos.Y) || !std::isfinite(Pos.Z))
			{
				continue;
			}
			PoseBox.Min.X = std::min(PoseBox.Min.X, Pos.X);
			PoseBox.Min.Y = std::min(PoseBox.Min.Y, Pos.Y);
			PoseBox.Min.Z = std::min(PoseBox.Min.Z, Pos.Z);
			PoseBox.Max.X = std::max(PoseBox.Max.X, Pos.X);
			PoseBox.Max.Y = std::max(PoseBox.Max.Y, Pos.Y);
			PoseBox.Max.Z = std::max(PoseBox.Max.Z, Pos.Z);
			bAnyBone = true;
		}

		if (!bAnyBone || !AreBoundsFinite(PoseBox))
		{
			return FallbackBindBox;
		}

		const FVector BindExtent = AreBoundsFinite(FallbackBindBox)
			? (FallbackBindBox.Max - FallbackBindBox.Min) * 0.5f
			: FVector(0.5f, 0.5f, 0.5f);
		const float Pad = std::max(0.2f, BindExtent.Size() * 0.2f);
		PoseBox.Min -= FVector(Pad, Pad, Pad);
		PoseBox.Max += FVector(Pad, Pad, Pad);
		return PoseBox;
	}

	USkeletalMesh* TryLoadSkeletalMeshCandidate(const FString& CandidatePath)
	{
		if (CandidatePath.empty())
		{
			return nullptr;
		}
		std::error_code Ec;
		if (!fs::is_regular_file(FPaths::FromUtf8(CandidatePath), Ec))
		{
			return nullptr;
		}
		FString Error;
		return Cast<USkeletalMesh>(UAssetManager::LoadFbxMesh(CandidatePath, Error));
	}

	// 다른 PC/경로에서 저장된 절대 경로(.../Content/...)도 현재 프로젝트 Content 기준으로 복원해 프리뷰 메시를 찾는다.
	USkeletalMesh* LoadPreviewSkeletalMesh(const FString& PreviewPath, const fs::path& AssetFilePath, const UAnimSequence* OptionalSeq)
	{
		if (!PreviewPath.empty())
		{
			if (USkeletalMesh* Mesh = TryLoadSkeletalMeshCandidate(FAssetUtils::ResolveAssetFilePath(PreviewPath)))
			{
				return Mesh;
			}

			std::string Normalized = PreviewPath;
			std::replace(Normalized.begin(), Normalized.end(), '\\', '/');
			const std::string Lower = ToLowerAscii(Normalized);
			const size_t ContentPos = Lower.rfind("/content/");
			if (ContentPos != std::string::npos)
			{
				const FString RelPath = Normalized.substr(ContentPos + 9);
				if (USkeletalMesh* Mesh = TryLoadSkeletalMeshCandidate(FAssetUtils::ResolveAssetFilePath(RelPath)))
				{
					return Mesh;
				}
			}

			const fs::path PreviewFileName = FPaths::FromUtf8(Normalized).filename();
			if (!PreviewFileName.empty() && !AssetFilePath.empty())
			{
				const fs::path LocalSibling = AssetFilePath.parent_path() / PreviewFileName;
				if (USkeletalMesh* Mesh = TryLoadSkeletalMeshCandidate(FPaths::ToUtf8(LocalSibling)))
				{
					return Mesh;
				}
			}
		}

		// PreviewPath가 비어 있거나 이동된 경우 같은 폴더의 스켈레탈 메시에서 호환 메시를 찾는다.
		if (!AssetFilePath.empty())
		{
			std::error_code DirEc;
			const fs::path ParentDir = AssetFilePath.parent_path();
			for (const fs::directory_entry& Entry : fs::directory_iterator(ParentDir, DirEc))
			{
				if (DirEc || !Entry.is_regular_file())
				{
					continue;
				}
				const FString CandidateUtf8 = FPaths::ToUtf8(Entry.path());
				const std::string LowerCandidate = ToLowerAscii(CandidateUtf8);
				if (!FFbxMeshBake::IsBakedPath(CandidateUtf8)
					|| (LowerCandidate.size() >= 15 && LowerCandidate.compare(LowerCandidate.size() - 15, 15, "_static.fbx.bin") == 0))
				{
					continue;
				}
				FString Error;
				if (USkeletalMesh* SiblingMesh = Cast<USkeletalMesh>(UAssetManager::LoadFbxMesh(CandidateUtf8, Error)))
				{
					if (!OptionalSeq || HasCompatibleSkeletonTrack(*OptionalSeq, *SiblingMesh))
					{
						return SiblingMesh;
					}
				}
			}
		}

		return nullptr;
	}

	UAnimSequence* FindPreviewSequenceInBlueprint(const FAnimBlueprintData& Data)
	{
		for (const FAnimBlueprintGraph& Graph : Data.Graphs)
		{
			for (const FAnimBlueprintNode& Node : Graph.Nodes)
			{
				if (Node.Type == EAnimBlueprintNodeType::SequencePlayer && !Node.SequencePath.empty())
				{
					if (UAnimSequence* Seq = UAssetManager::LoadAnimSequence(Node.SequencePath))
					{
						return Seq;
					}
				}
			}
		}
		return nullptr;
	}

	bool SetupSkeletalThumbnail(
		USkeletalMesh* SkeletalMesh,
		const UAnimSequence* OptionalPoseSeq,
		UStaticMeshComponent* StaticComp,
		USkeletalMeshComponent* SkelComp,
		FBox& OutBounds)
	{
		if (!SkeletalMesh)
		{
			return false;
		}
		const auto& LODs = SkeletalMesh->GetRenderData().LODRenderData;
		if (LODs.IsEmpty() || LODs[0].BindVertices.IsEmpty() || LODs[0].Indices.IsEmpty())
		{
			return false;
		}

		StaticComp->SetStaticMesh(nullptr);
		SkelComp->SetSkeletalMesh(SkeletalMesh);
		SkelComp->ResetPose();

		if (OptionalPoseSeq && HasCompatibleSkeletonTrack(*OptionalPoseSeq, *SkeletalMesh))
		{
			const float Length = OptionalPoseSeq->GetPlayLength();
			const float SampleTime = Length > 0.0f ? Length * 0.35f : 0.0f;
			FCompactPose Pose;
			OptionalPoseSeq->GetAnimationPose(SampleTime, Pose, SkeletalMesh->GetRefSkeleton());
			SkelComp->SetBoneSpaceTransforms(Pose);
			SkelComp->RefreshBoneTransformsNow();
			OutBounds = ComputePosedSkeletonBounds(*SkelComp, LODs[0].BindAABB);
		}
		else
		{
			SkelComp->RefreshBoneTransformsNow();
			OutBounds = LODs[0].BindAABB;
		}

		return AreBoundsFinite(OutBounds);
	}
}

class FThumbnailViewportClient final : public FEditorViewportClient
{
public:
	explicit FThumbnailViewportClient(FPreviewScene& InScene)
		: FEditorViewportClient(InScene)
	{
		EngineShowFlags.bEnableFog = false;
		EngineShowFlags.bEnableFXAA = true;
		EngineShowFlags.bShowVignette = false;
		EngineShowFlags.bShowCameraOverlay = false;
		EngineShowFlags.bShowSkeletalMesh = true;
	}

	void ApplySettings(const FEditorSettings* Settings)
	{
		if (!Settings)
		{
			return;
		}
		EngineShowFlags.LightingModel = Settings->LightingModel;
		EngineShowFlags.bGammaCorrection = Settings->bGammaCorrection;
		EngineShowFlags.DisplayGamma = Settings->DisplayGamma;
	}

protected:
	void Draw(const FSceneView* View, FScenePassCallbacks& Callbacks) override
	{
		Callbacks.Overlays.EditorGrid = []()
		{
			FShaderProgram* CheckerShader = FRenderResourceManager::GetShaderProgram(
				"EngineResources/Shaders/ThumbnailCheckerShader.hlsl");
			if (!CheckerShader || !CheckerShader->VertexShader || !CheckerShader->PixelShader)
			{
				return;
			}

			FPipelineState Pipeline{};
			Pipeline.Shader = CheckerShader;
			Pipeline.Topology = EPrimitiveTopology::TriangleList;
			Pipeline.RasterizerState = ERasterizerState::SolidNone;
			Pipeline.BlendState = EBlendState::Opaque;
			Pipeline.DepthStencilState = EDepthStencilState::ReadOnly;
			RenderCommand::BindPipelineState(Pipeline);
			RenderCommand::BindVertexBuffer(nullptr);
			RenderCommand::Draw(3);
		};
	}
};

FMeshThumbnailRenderer::FMeshThumbnailRenderer() = default;

FMeshThumbnailRenderer::~FMeshThumbnailRenderer()
{
	Release();
}

bool FMeshThumbnailRenderer::IsSupportedMeshFile(const fs::path& FilePath, const FString& Utf8Path)
{
	return IsObjExtension(FilePath)
		|| FFbxMeshBake::IsBakedPath(Utf8Path)
		|| AnimSequenceFile::IsAnimSequencePath(Utf8Path)
		|| BlendSpaceFile::IsBlendSpacePath(Utf8Path)
		|| AnimBlueprintFile::IsAnimBlueprintPath(Utf8Path);
}

ImTextureID FMeshThumbnailRenderer::GetOrRequestThumbnail(
	const fs::path& FilePath,
	const FString& Utf8Path,
	fs::file_time_type LastWriteTime)
{
	const auto It = Cache.find(Utf8Path);
	if (It != Cache.end() && It->second.LastWriteTime == LastWriteTime)
	{
		if (It->second.Texture && It->second.Texture->GetSRV())
		{
			return reinterpret_cast<ImTextureID>(It->second.Texture->GetSRV());
		}
		if (It->second.bFailed)
		{
			return ImTextureID_Invalid;
		}
	}

	auto QueueIt = std::find_if(PendingQueue.begin(), PendingQueue.end(),
		[&Utf8Path](const FPendingRequest& Req) { return Req.Utf8Path == Utf8Path; });
	if (QueueIt == PendingQueue.end())
	{
		PendingQueue.push_back({ FilePath, Utf8Path, LastWriteTime });
	}
	else if (QueueIt->LastWriteTime != LastWriteTime)
	{
		QueueIt->FilePath = FilePath;
		QueueIt->LastWriteTime = LastWriteTime;
	}

	// 파일이 막 갱신된 경우 새로 찍히기 전까지는 직전 텍스처라도 유지해 깜빡임을 줄인다.
	if (It != Cache.end() && It->second.Texture && It->second.Texture->GetSRV())
	{
		return reinterpret_cast<ImTextureID>(It->second.Texture->GetSRV());
	}
	return ImTextureID_Invalid;
}

void FMeshThumbnailRenderer::RenderPending(UEditorEngine& Engine, FRenderer* Renderer)
{
	if (PendingQueue.empty() || !Renderer || bPreviewInitFailed)
	{
		return;
	}

	const FPendingRequest Request = std::move(PendingQueue.front());
	PendingQueue.erase(PendingQueue.begin());

	const auto It = Cache.find(Request.Utf8Path);
	if (It != Cache.end() && It->second.LastWriteTime == Request.LastWriteTime &&
		((It->second.Texture && It->second.Texture->GetSRV()) || It->second.bFailed))
	{
		return;
	}

	RenderMeshToCache(Request, Engine, Renderer);
}

bool FMeshThumbnailRenderer::EnsurePreviewScene(UEditorEngine& Engine)
{
	if (PreviewScene && ViewportClient && StaticMeshComp && SkeletalMeshComp)
	{
		return true;
	}
	if (bPreviewInitFailed)
	{
		return false;
	}

	PreviewScene = MakeUnique<FPreviewScene>();
	if (!PreviewScene->Initialize(&Engine))
	{
		PreviewScene.reset();
		bPreviewInitFailed = true;
		return false;
	}
	PreviewScene->Setup();

	AActor* PreviewActor = PreviewScene->AddActor(AActor::StaticClass());
	if (!PreviewActor)
	{
		PreviewScene->Release();
		PreviewScene.reset();
		bPreviewInitFailed = true;
		return false;
	}
	PreviewActor->SetActorEnableCollision(false);

	StaticMeshComp = Cast<UStaticMeshComponent>(
		PreviewActor->AddComponent(UStaticMeshComponent::StaticClass(), "ThumbnailStaticMesh"));
	SkeletalMeshComp = Cast<USkeletalMeshComponent>(
		PreviewActor->AddComponent(USkeletalMeshComponent::StaticClass(), "ThumbnailSkeletalMesh"));
	if (!StaticMeshComp || !SkeletalMeshComp)
	{
		StaticMeshComp = nullptr;
		SkeletalMeshComp = nullptr;
		PreviewScene->Release();
		PreviewScene.reset();
		bPreviewInitFailed = true;
		return false;
	}

	ViewportClient = MakeUnique<FThumbnailViewportClient>(*PreviewScene);
	ViewportClient->SetViewportType(ELevelViewportType::Perspective);
	ViewportClient->Resize(ThumbnailResolution, ThumbnailResolution);

	return true;
}

void FMeshThumbnailRenderer::RenderMeshToCache(const FPendingRequest& Request, UEditorEngine& Engine, FRenderer* Renderer)
{
	FThumbnailEntry& Entry = Cache[Request.Utf8Path];
	Entry.LastWriteTime = Request.LastWriteTime;
	Entry.bFailed = true;

	if (!EnsurePreviewScene(Engine))
	{
		return;
	}

	FBox BoundsBox{};
	bool bValidMesh = false;

	if (AnimSequenceFile::IsAnimSequencePath(Request.Utf8Path))
	{
		UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Request.Utf8Path);
		if (Sequence && Sequence->GetDataModel())
		{
			USkeletalMesh* PreviewMesh = LoadPreviewSkeletalMesh(Sequence->GetPreviewMeshPath(), Request.FilePath, Sequence);
			bValidMesh = SetupSkeletalThumbnail(PreviewMesh, Sequence, StaticMeshComp, SkeletalMeshComp, BoundsBox);
		}
	}
	else if (BlendSpaceFile::IsBlendSpacePath(Request.Utf8Path))
	{
		if (UBlendSpace1D* BlendSpace = UAssetManager::LoadBlendSpace(Request.Utf8Path))
		{
			const UAnimSequence* SampleSeq = nullptr;
			for (const FBlendSample& Sample : BlendSpace->GetSampleData())
			{
				if (Sample.Animation)
				{
					SampleSeq = Sample.Animation;
					break;
				}
			}
			USkeletalMesh* PreviewMesh = LoadPreviewSkeletalMesh(BlendSpace->GetPreviewMeshPath(), Request.FilePath, SampleSeq);
			bValidMesh = SetupSkeletalThumbnail(PreviewMesh, SampleSeq, StaticMeshComp, SkeletalMeshComp, BoundsBox);
		}
	}
	else if (AnimBlueprintFile::IsAnimBlueprintPath(Request.Utf8Path))
	{
		FAnimBlueprintData Data;
		FString Error;
		if (AnimBlueprintFile::Read(Request.FilePath, Data, Error))
		{
			const UAnimSequence* SampleSeq = FindPreviewSequenceInBlueprint(Data);
			USkeletalMesh* PreviewMesh = LoadPreviewSkeletalMesh(Data.PreviewMeshPath, Request.FilePath, SampleSeq);
			bValidMesh = SetupSkeletalThumbnail(PreviewMesh, SampleSeq, StaticMeshComp, SkeletalMeshComp, BoundsBox);
		}
	}
	else
	{
		URenderAsset* Asset = LoadMeshAssetForThumbnail(Request.FilePath, Request.Utf8Path);
		if (!Asset)
		{
			return;
		}

		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
		{
			const auto& MeshData = StaticMesh->GetMeshData();
			if (!MeshData.Vertices.IsEmpty() && !MeshData.Indices.IsEmpty())
			{
				StaticMeshComp->SetStaticMesh(StaticMesh);
				SkeletalMeshComp->SetSkeletalMesh(nullptr);
				BoundsBox = StaticMesh->GetRenderBounds();
				bValidMesh = AreBoundsFinite(BoundsBox);
			}
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
		{
			bValidMesh = SetupSkeletalThumbnail(SkeletalMesh, nullptr, StaticMeshComp, SkeletalMeshComp, BoundsBox);
		}
	}

	if (!bValidMesh)
	{
		StaticMeshComp->SetStaticMesh(nullptr);
		SkeletalMeshComp->SetSkeletalMesh(nullptr);
		return;
	}

	static_cast<FThumbnailViewportClient*>(ViewportClient.get())->ApplySettings(Engine.GetEditorSettings());

	PreviewScene->Tick(0.0f);
	if (UWorld* PreviewWorld = PreviewScene->GetWorld())
	{
		PreviewWorld->GetScene().UpdateAllTransforms();
	}

	const FVector Extent = (BoundsBox.Max - BoundsBox.Min) * 0.5f;
	const FBoxSphereBounds Bounds{
		(BoundsBox.Min + BoundsBox.Max) * 0.5f,
		Extent,
		Extent.Size()
	};

	ViewportClient->Resize(ThumbnailResolution, ThumbnailResolution);
	if (!ViewportClient->GetViewTargets().IsValidFor(ThumbnailResolution, ThumbnailResolution))
	{
		StaticMeshComp->SetStaticMesh(nullptr);
		SkeletalMeshComp->SetSkeletalMesh(nullptr);
		return;
	}

	ViewportClient->SetViewRotation(FRotator(22.0f, 200.0f, 0.0f));
	ViewportClient->FocusOnBounds(Bounds, false);
	ViewportClient->Draw(Renderer);

	FTexture2D* RenderedColor = ViewportClient->GetColorTarget();
	if (RenderedColor && RenderedColor->GetRawPtr())
	{
		FTextureDesc Desc{};
		Desc.Width = ThumbnailResolution;
		Desc.Height = ThumbnailResolution;
		Desc.Format = ETextureFormat::R8G8B8A8_UNorm;
		Desc.BindFlags = ETextureBindFlags::ShaderResource | ETextureBindFlags::RenderTarget;

		TUniquePtr<FTexture2D> CachedTexture = RenderCommand::CreateTexture2D(Desc);
		if (CachedTexture && CachedTexture->GetRawPtr() && CachedTexture->GetSRV())
		{
			RenderCommand::CopyTexture(CachedTexture.get(), RenderedColor);
			Entry.Texture = std::move(CachedTexture);
			Entry.bFailed = false;
		}
	}

	StaticMeshComp->SetStaticMesh(nullptr);
	SkeletalMeshComp->SetSkeletalMesh(nullptr);
}

void FMeshThumbnailRenderer::ClearPendingQueue()
{
	PendingQueue.clear();
}

void FMeshThumbnailRenderer::InvalidatePath(const FString& Utf8Path)
{
	Cache.erase(Utf8Path);
	PendingQueue.erase(
		std::remove_if(PendingQueue.begin(), PendingQueue.end(),
			[&Utf8Path](const FPendingRequest& Req) { return Req.Utf8Path == Utf8Path; }),
		PendingQueue.end());
}

void FMeshThumbnailRenderer::InvalidateDirectoryPrefix(const FString& Utf8DirPrefix)
{
	if (Utf8DirPrefix.empty())
	{
		return;
	}

	const auto MatchesPrefix = [&Utf8DirPrefix](const std::string& Path)
	{
		if (Path == Utf8DirPrefix)
		{
			return true;
		}
		if (Path.size() > Utf8DirPrefix.size() && Path.compare(0, Utf8DirPrefix.size(), Utf8DirPrefix) == 0)
		{
			const char Next = Path[Utf8DirPrefix.size()];
			return Next == '/' || Next == '\\';
		}
		return false;
	};

	for (auto It = Cache.begin(); It != Cache.end();)
	{
		if (MatchesPrefix(It->first))
		{
			It = Cache.erase(It);
		}
		else
		{
			++It;
		}
	}

	PendingQueue.erase(
		std::remove_if(PendingQueue.begin(), PendingQueue.end(),
			[&MatchesPrefix](const FPendingRequest& Req) { return MatchesPrefix(Req.Utf8Path); }),
		PendingQueue.end());
}

void FMeshThumbnailRenderer::Release()
{
	PendingQueue.clear();
	Cache.clear();
	StaticMeshComp = nullptr;
	SkeletalMeshComp = nullptr;
	ViewportClient.reset();
	if (PreviewScene)
	{
		PreviewScene->Release();
		PreviewScene.reset();
	}
	bPreviewInitFailed = false;
}
