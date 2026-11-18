#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Viewports/EditorViewportClient.h"


class FSkeletalMeshEditor : public FSkeletalPreviewEditor
{
public:
	FSkeletalMeshEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
		const FString& InAssetPath, USkeletalMesh& InMesh)
		: FSkeletalPreviewEditor(InEditorEngine, InEditorManager, InAssetPath)
	{
		Mesh = &InMesh;
	}

	const char* GetEditorTypeName() const override { return "SkeletalMesh"; }
	bool UsesPreviewSkybox() const override { return true; }
	const FGizmo* GetPreviewGizmo() const override { return BoneGizmo.GetTarget() ? &BoneGizmo : nullptr; }

	// 선택한 본의 프리뷰 포즈를 부모 기준 로컬 변환으로 조회·수정합니다.
	bool GetSelectedBoneLocalTransform(FTransform& OutTransform) const;
	void SetSelectedBoneLocalTransform(const FTransform& Transform);
	void ResetPose();
	void ResetBone(int32 BoneIndex);

	void CommitBonePoseUndo(bool bInteractionActive);
	void UndoPoseEdit();
	void RedoPoseEdit();
	bool CanUndoPoseEdit() const { return !PoseUndoStack.IsEmpty(); }
	bool CanRedoPoseEdit() const { return !PoseRedoStack.IsEmpty(); }

	void SetGizmoMode(EGizmoMode Mode);
	void SetGizmoSpace(EGizmoSpace Space);
	EGizmoMode  GetGizmoMode() const { return BoneGizmo.GetMode(); }
	EGizmoSpace GetGizmoSpace() const { return BoneGizmo.GetSpace(); }
	bool IsGizmoDragging() const { return BoneGizmo.IsUsing(); }

	// 에셋 브라우저에서 선택한 애니메이션 시퀀스 에디터를 연다.
	void OpenAnimationAsset(const FString& InAssetPath);

protected:
	// 이 에디터에 연결된 탭을 생성합니다.
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;     // InitializePreview() 후 BoneGizmoTarget 생성
	void ShutdownResources() override;       // 기즈모 정리 후 Super

	void TickAfterWorld(float DeltaTime) override;
	bool IsViewportInteractionBusy() const override { return BoneGizmo.IsUsing(); }
	void OnBoneSelected() override { BoneGizmo.EndDrag(); SyncBoneGizmo(); }
	void CancelViewportInteraction() override { BoneGizmo.EndDrag(); }

private:
	void SyncBoneGizmo();
	bool UpdateBoneGizmo();
	void ApplyBoneGizmoTransform();
	FQuat GetBoneWorldRotation(int32 BoneIndex) const;
	TArray<FTransform> CaptureBonePose() const;
	void ApplyBonePose(const TArray<FTransform>& Pose);
	void ResetBonePoseHistory();

	USceneComponent* BoneGizmoTarget = nullptr;
	FGizmo BoneGizmo;
	FTransform DragStartBoneTransform;
	FMatrix DragParentInverse;
	FQuat DragParentRotation;

	TArray<TArray<FTransform>> PoseUndoStack;
	TArray<TArray<FTransform>> PoseRedoStack;
	TArray<FTransform> CommittedBonePose;
	bool bPoseUndoPending = false;
	static constexpr int32 MaxUndoSteps = 100;
};
