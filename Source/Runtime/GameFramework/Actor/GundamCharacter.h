#pragma once

#include "GameFramework/Character.h"
#include "GundamCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UParticleSystemComponent;
class AGundamFunnel;

enum class EGundamWeapon : uint8
{
	BeamRifle = 1,
	Funnel = 2,
};

UCLASS()
class AGundamCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AGundamCharacter();

	// 마우스 좌우 회전은 Tick에서 ControlRotation.Yaw와 동기화하고, 상하 Pitch는 카메라(SpringArm)만 돈다
	virtual void AddControllerYawInput(float Value) override {}
	virtual void AddControllerPitchInput(float Value) override {}

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual FRotator GetMovementInputRotation() const override;

	UPROPERTY(EditAnywhere)
	float BoosterUpAccel = 22.0f;

	UPROPERTY(EditAnywhere)
	float MaxBoosterUpSpeed = 8.5f;

	UPROPERTY(EditAnywhere)
	float FastDescendAccel = 76.0f;

	UPROPERTY(EditAnywhere)
	float MaxFastDescendSpeed = 36.0f;

	UPROPERTY(EditAnywhere)
	float NormalWalkSpeed = 800.0f;

	UPROPERTY(EditAnywhere)
	float DashSpeed = 1750.0f;

	UPROPERTY(EditAnywhere)
	float DoubleTapWindow = 0.28f;

	UPROPERTY(EditAnywhere)
	float AirStopDamping = 12.0f;

	UPROPERTY(EditAnywhere)
	float MaxTiltAngle = 18.0f;

	UPROPERTY(EditAnywhere)
	float TiltInterpSpeed = 10.0f;

	UPROPERTY(EditAnywhere)
	float FunnelSpeed = 24.0f;

	UPROPERTY(EditAnywhere)
	float FunnelOrbitRadius = 1.35f;

	UPROPERTY(EditAnywhere)
	float FunnelRespawnDelay = 0.65f;

	UPROPERTY(EditAnywhere)
	int32 FunnelCount = 6;

	USpringArmComponent* SpringArmComponent = nullptr;
	UCameraComponent* CameraComponent = nullptr;
	UParticleSystemComponent* BackThrusterParticle = nullptr;
	UParticleSystemComponent* RightFootThrusterParticle = nullptr;
	UParticleSystemComponent* LeftFootThrusterParticle = nullptr;
	UParticleSystemComponent* WingUpperLeftParticle = nullptr;
	UParticleSystemComponent* WingLowerLeftParticle = nullptr;
	UParticleSystemComponent* WingUpperRightParticle = nullptr;
	UParticleSystemComponent* WingLowerRightParticle = nullptr;
	UParticleSystemComponent* DashShockwaveParticle = nullptr;

private:
	void EnsureVisualAssets();
	void UpdateControlRotationSync();
	void UpdateDashInput(float DeltaTime);
	void UpdateAirDamping(float DeltaTime);
	void UpdateBoosterAndDescend(float DeltaTime, bool bSpaceDown, bool bShiftDown);
	void UpdateMeshTilt(float DeltaTime, bool bBoosting, bool bDescending);
	void UpdateThrusters(float DeltaTime, bool bBoosting);
	void UpdateWingEffects(float DeltaTime, bool bBoosting);
	void TriggerDashShockwave();
	void DeployOrbitingFunnels();
	void ClearOrbitingFunnels();
	void UpdateOrbitingFunnels(float DeltaTime);
	FVector ComputeFunnelOrbitSlot(int32 Index) const;
	void HandleWeaponInput();
	void FireBeam();
	void FireFunnels();

	EGundamWeapon CurrentWeapon = EGundamWeapon::BeamRifle;
	FRotator CurrentMeshTilt = FRotator(0.0f, 0.0f, 0.0f);
	float CurrentThrusterScale = 0.35f;
	float CurrentWingScale = 0.89f;

	TArray<AGundamFunnel*> OrbitingFunnels;
	float FunnelOrbitTime = 0.0f;
	float FunnelRespawnTimer = -1.0f;

	float InputClock = 0.0f;
	float LastTapTime[4] = { -10.0f, -10.0f, -10.0f, -10.0f };
	bool bWasMoveKeyDown[4] = { false, false, false, false };
	int32 DashKeyIndex = -1;
	float DashElapsed = 0.0f;

	int32 BackBoneIndex = -1;
	int32 RightFootBoneIndex = -1;
	int32 LeftFootBoneIndex = -1;
	int32 WingLeftBoneIndex = -1;
	int32 WingRightBoneIndex = -1;

	bool bWasNum1Down = false;
	bool bWasNum2Down = false;
	bool bWasLButtonDown = false;
};
