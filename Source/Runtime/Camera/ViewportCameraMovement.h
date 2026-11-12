#pragma once

#include "Math/EngineMath.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"

// Input ownership and key storage stay with the caller. This helper only moves a camera.
struct FViewportCameraInput
{
    bool bCaptured = false;
    bool bLeftDrag = false;   // UE 좌클릭 드래그: 원근은 앞뒤 이동 + 좌우 회전, 직교는 화면 이동
    bool bAllowWheel = false;
    bool bForward = false;
    bool bBackward = false;
    bool bRight = false;
    bool bLeft = false;
    bool bUp = false;
    bool bDown = false;
    float MouseX = 0.0f;
    float MouseY = 0.0f;
    float Wheel = 0.0f;
};

struct FViewportCameraState
{
    FVector Location;
    FRotator Rotation;
    float OrthoWidth = 1.0f;
    float ViewWidth = 800.0f;
    bool bPerspective = true;
};

inline void ApplyViewportCameraMovement(FViewportCameraState& Camera,
    const FViewportCameraInput& Input, float DeltaTime, float MoveSpeed, float MouseSensitivity)
{
	if (Input.bCaptured && Camera.bPerspective)
	{
		Camera.Rotation.Yaw += Input.MouseX * MouseSensitivity;
		Camera.Rotation.Pitch += Input.MouseY * MouseSensitivity;
	}

    if (Input.bLeftDrag && !Input.bCaptured)
    {
        if (Camera.bPerspective)
        {
            // 좌우로 끌면 회전, 위아래로 끌면 수평면 위에서 앞뒤로 이동한다 (UE와 같다).
            Camera.Rotation.Yaw += Input.MouseX * MouseSensitivity;
            const FVector Forward = Camera.Rotation.Quaternion().GetForwardVector();
            FVector Flat(Forward.X, Forward.Y, 0.0f);
            if (Flat.Size() > 0.0001f)
                Camera.Location += Flat.Normalized() * (-Input.MouseY * 0.02f * MoveSpeed);
        }
        else
        {
            const FQuat DragOrientation = Camera.Rotation.Quaternion();
            const float UnitsPerPixel = Camera.OrthoWidth / Camera.ViewWidth;
            Camera.Location += DragOrientation.GetRightVector() * (-Input.MouseX * UnitsPerPixel) +
                DragOrientation.GetUpVector() * (Input.MouseY * UnitsPerPixel);
        }
    }

    const FQuat Orientation = Camera.Rotation.Quaternion();
    const FVector Forward = Orientation.GetForwardVector();
    const FVector Right = Orientation.GetRightVector();
    const FVector Up = Orientation.GetUpVector();
    if (Input.bCaptured)
    {
        if (!Camera.bPerspective)
        {
            const float UnitsPerPixel = Camera.OrthoWidth / Camera.ViewWidth;
            Camera.Location += Right * (-Input.MouseX * UnitsPerPixel) +
                Up * (Input.MouseY * UnitsPerPixel);
        }
        FVector Direction = FVector::ZeroVector;
        const FVector ForwardAxis = Camera.bPerspective ? Forward : Up;
        if (Input.bForward) Direction += ForwardAxis;
        if (Input.bBackward) Direction -= ForwardAxis;
        if (Input.bRight) Direction += Right;
        if (Input.bLeft) Direction -= Right;
        if (Camera.bPerspective && Input.bUp) Direction += Up;
        if (Camera.bPerspective && Input.bDown) Direction -= Up;
        if (Direction.Size() > 0.0001f)
            Camera.Location += Direction.Normalized() * (MoveSpeed * DeltaTime);
    }
    if (Input.bAllowWheel && Input.Wheel != 0.0f)
    {
        if (Camera.bPerspective)
            Camera.Location += Forward * (Input.Wheel * 0.01f * MoveSpeed);
        else
            Camera.OrthoWidth = FMath::Clamp(Camera.OrthoWidth *
                (Input.Wheel > 0.0f ? 0.9f : 1.1f), 0.1f, 100000.0f);
    }
}
