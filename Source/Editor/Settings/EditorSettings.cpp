#include "EnginePCH.h"
#include "Core/Misc/Paths.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewports/ViewportsPanel.h"

// 종료 시 렌더·에디터·뷰포트 설정을 함께 저장한다.
FEditorSettingsManager::~FEditorSettingsManager()
{
	ViewportsPanel = nullptr;
	SaveSettings();
}

// 설정 파일을 읽어 패널 상태를 초기화한다.
bool FEditorSettingsManager::Init()
{
	LoadSettings();
	return true;
}


// 설정 패널의 프레임 갱신 진입점이다.
bool FEditorSettingsManager::SaveSettings() const
{
	FEditorSettings Snapshot = Settings;
	ReadViewportSettings(Snapshot);
	std::error_code Ec;
	std::filesystem::create_directories(FPaths::EditorSettingsFile().parent_path(), Ec);
	std::ofstream File(FPaths::EditorSettingsFile());

	if (!File.is_open())
	{
		HTR_LOG(Error, "Failed to open editor.ini for saving.");
		return false;
	}

	File << "[Rendering]\n";
	File << "DrawPrimitives=" << Settings.bDrawPrimitives << "\n";
	File << "CacheShadows=" << Settings.bCacheShadows << "\n";
	File << "PcfMode=" << static_cast<uint32_t>(Settings.PcfMode) << "\n";
	File << "DrawBoundingBox=" << Settings.bDrawBoundingBox << "\n";
	File << "ShowUUID=" << Settings.bShowUUID << "\n";
	File << "ShowCollision=" << Settings.bShowCollision << "\n";
	File << "ShowRootDebug=" << Settings.bShowRootDebug << "\n";
	File << "DrawDecals=" << Settings.bDrawDecals << "\n";
	File << "DecalCulling=" << Settings.bDecalCulling << "\n";
	File << "DecalNarrowCulling=" << Settings.bDecalNarrowCulling << "\n";
	File << "DecalBVHCulling=" << Settings.bDecalBVHCulling << "\n";
	File << "DecalOBBTree=" << Settings.bDecalOBBTree << "\n";
	File << "DrawBatchLine=" << Settings.bDrawBatchLine << "\n";
	File << "DrawCascadeBounds=" << Settings.bDrawCascadeBounds << "\n";
	File << "SceneDepthMaxRange=" << Settings.SceneDepthMaxRange << "\n";
	File << "EnableFXAA=" << Settings.bEnableFXAA << "\n";

	File << "GammaCorrection=" << Settings.bGammaCorrection << "\n";
	File << "DisplayGamma=" << Settings.DisplayGamma << "\n";
	File << "ShowVignette=" << Settings.bShowVignette << "\n";
	File << "ShowCameraOverlay=" << Settings.bShowCameraOverlay << "\n";
	File << "ShowSkeletalMesh=" << Settings.bShowSkeletalMesh << "\n";

	File << "LightCullingMode=" << (Settings.LightCullingMode == ELightCullingMode::Off ? "Off" : "Tiled") << "\n";
	File << "LightingModel=" << (Settings.LightingModel == ELightingModel::Gouraud ? "Gouraud" :
		Settings.LightingModel == ELightingModel::Lambert ? "Lambert" :
		Settings.LightingModel == ELightingModel::Unlit ? "Unlit" : "Phong") << "\n";
	File << "\n";

	File << "[Editor]\n";
	File << "CameraMoveSpeed=" << Settings.CameraSpeed << "\n";
	File << "CameraRotateSensitivity=" << Settings.MouseSensitivity << "\n";
	File << "GridSpacing=" << Settings.GridSpacing << "\n";
	File << "\n";

	File << "[MultipleViewports]\n";
	File << "Horizontal=" << Snapshot.MultipleViewportsHorizontal << "\n";
	File << "Vertical=" << Snapshot.MultipleViewportsVertical << "\n";
	File << "Layout=" << GetLayoutModeName(static_cast<ELayoutMode>(Snapshot.ViewportLayout)) << "\n";
	File << "SingleViewIndex=" << Snapshot.MultipleViewportsSingleViewIndex << "\n";

	for (int32 Index = 0; Index < 4; ++Index)
    {
        File << "View" << Index << "Fov=" << Snapshot.ViewFov[Index] << "\n";
        File << "View" << Index << "OrthoWidth=" << Snapshot.ViewOrthoWidth[Index] << "\n";
        File << "View" << Index << "Preset=" << Snapshot.ViewPreset[Index] << "\n";
        File << "View" << Index << "Mode=" << Snapshot.ViewMode[Index] << "\n";
        File << "View" << Index << "LocationX=" << Snapshot.ViewLocation[Index].X << "\n";
        File << "View" << Index << "LocationY=" << Snapshot.ViewLocation[Index].Y << "\n";
        File << "View" << Index << "LocationZ=" << Snapshot.ViewLocation[Index].Z << "\n";
        File << "View" << Index << "RotationX=" << Snapshot.ViewRotation[Index].X << "\n";
        File << "View" << Index << "RotationY=" << Snapshot.ViewRotation[Index].Y << "\n";
        File << "View" << Index << "RotationZ=" << Snapshot.ViewRotation[Index].Z << "\n";
        File << "View" << Index << "RotationW=" << Snapshot.ViewRotation[Index].W << "\n";
    }

	File << "DirectionalShadowMapSizeLevel=" << Settings.DirectionalShadowMapSizeLevel << '\n';
	File << "PointShadowMapSizeLevel=" << Settings.PointShadowMapSizeLevel << '\n';
	File << "SpotShadowMapSizeLevel=" << Settings.SpotShadowMapSizeLevel << '\n';

    File.close();
	return true;
}

// 파일의 키를 파싱해 렌더·카메라·레이아웃 설정을 복원한다.
bool FEditorSettingsManager::LoadSettings()
{
	auto FilePath = FPaths::EditorSettingsFile();
	if (!std::filesystem::exists(FilePath)) FilePath = FPaths::ProjectSavedDir() / "Editor/editor.ini";
	if (!std::filesystem::exists(FilePath)) FilePath = FPaths::ProjectDir() / "editor.ini";
	std::ifstream File(FilePath);
	if (!File.is_open())
	{
		return false;
	}

	uint8 LocationComponentMasks[4]{};
	uint8 RotationComponentMasks[4]{};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Settings.bViewLocationSaved[Index] = false;
		Settings.bViewRotationSaved[Index] = false;
	}

	std::string Line;
	while (std::getline(File, Line))
	{
		if (Line.empty() || Line[0] == ';' || Line[0] == '[') continue;

		std::istringstream Iss(Line);
		std::string Key;
		if (std::getline(Iss, Key, '='))
		{
			std::string ValueStr;
			if (std::getline(Iss, ValueStr))
			{
				// 새 View 키는 NaN·잘못된 숫자·범위 밖 값을 무시한다.
                for (int32 Index = 0; Index < 4; ++Index)
                {
                    FString Prefix = "View";
                    Prefix += static_cast<char>('0' + Index);
                    std::istringstream Number(ValueStr);
                    float Value = 0;
                    if (!(Number >> Value) || !std::isfinite(Value)) continue;
                    if (Key == Prefix + "Fov" && Value >= 1 && Value <= 179)
                        Settings.ViewFov[Index] = Value;
                    else if (Key == Prefix + "OrthoWidth" && Value >= 0.01f)
                        // 파일 값은 절대 상한으로 먼저 제한하고, 실제 View 비율 상한은 Adapter가 적용한다.
                        Settings.ViewOrthoWidth[Index] = FMath::Clamp(Value, 0.01f, 1000000.0f);
                    else if (Key == Prefix + "Preset" && Value >= 0 && Value <= 7 && Value == static_cast<int32>(Value))
                        Settings.ViewPreset[Index] = static_cast<int32>(Value);
					else if (Key == Prefix + "Mode" && Value >= 0 &&
						Value < static_cast<int32>(EViewportMode::Count) && Value == static_cast<int32>(Value))
						Settings.ViewMode[Index] = static_cast<int32>(Value);
                    else if (Key == Prefix + "LocationX")
                    {
                        Settings.ViewLocation[Index].X = Value;
                        LocationComponentMasks[Index] |= 1 << 0;
                    }
                    else if (Key == Prefix + "LocationY")
                    {
                        Settings.ViewLocation[Index].Y = Value;
                        LocationComponentMasks[Index] |= 1 << 1;
                    }
                    else if (Key == Prefix + "LocationZ")
                    {
                        Settings.ViewLocation[Index].Z = Value;
                        LocationComponentMasks[Index] |= 1 << 2;
                    }
                    else if (Key == Prefix + "RotationX")
                    {
                        Settings.ViewRotation[Index].X = Value;
                        RotationComponentMasks[Index] |= 1 << 0;
                    }
                    else if (Key == Prefix + "RotationY")
                    {
                        Settings.ViewRotation[Index].Y = Value;
                        RotationComponentMasks[Index] |= 1 << 1;
                    }
                    else if (Key == Prefix + "RotationZ")
                    {
                        Settings.ViewRotation[Index].Z = Value;
                        RotationComponentMasks[Index] |= 1 << 2;
                    }
                    else if (Key == Prefix + "RotationW")
                    {
                        Settings.ViewRotation[Index].W = Value;
                        RotationComponentMasks[Index] |= 1 << 3;
                    }
                }
                if (Key == "DrawPrimitives") Settings.bDrawPrimitives = std::stoi(ValueStr);
				else if (Key == "CacheShadows") Settings.bCacheShadows = std::stoi(ValueStr);
				else if (Key == "PcfMode")
				{
					const int Mode = std::stoi(ValueStr);
					if (Mode >= 0 && Mode <= static_cast<int>(EPcfMode::PCSS))
						Settings.PcfMode = static_cast<EPcfMode>(Mode);
				}
				else if (Key == "DrawBoundingBox") Settings.bDrawBoundingBox = std::stoi(ValueStr);
				else if (Key == "ShowUUID") Settings.bShowUUID = std::stoi(ValueStr);
				else if (Key == "ShowCollision") Settings.bShowCollision = std::stoi(ValueStr);
				else if (Key == "ShowRootDebug") Settings.bShowRootDebug = std::stoi(ValueStr);
				else if (Key == "DrawDecals") Settings.bDrawDecals = std::stoi(ValueStr);
				else if (Key == "DecalCulling") Settings.bDecalCulling = std::stoi(ValueStr);
				else if (Key == "DecalNarrowCulling") Settings.bDecalNarrowCulling = std::stoi(ValueStr);
				else if (Key == "DecalBVHCulling") Settings.bDecalBVHCulling = std::stoi(ValueStr);
				else if (Key == "DecalOBBTree") Settings.bDecalOBBTree = std::stoi(ValueStr);
				else if (Key == "DrawBatchLine") Settings.bDrawBatchLine = std::stoi(ValueStr);
				else if (Key == "DrawCascadeBounds") Settings.bDrawCascadeBounds = std::stoi(ValueStr);
				else if (Key == "SceneDepthMaxRange") Settings.SceneDepthMaxRange = std::stof(ValueStr);
				else if (Key == "EnableFXAA") Settings.bEnableFXAA = std::stoi(ValueStr);

				else if (Key == "GammaCorrection")   Settings.bGammaCorrection = std::stoi(ValueStr);
				else if (Key == "DisplayGamma")      Settings.DisplayGamma = std::stof(ValueStr);
				else if (Key == "ShowVignette")      Settings.bShowVignette = std::stoi(ValueStr);
				else if (Key == "ShowCameraOverlay") Settings.bShowCameraOverlay = std::stoi(ValueStr);
				else if (Key == "ShowSkeletalMesh")  Settings.bShowSkeletalMesh = std::stoi(ValueStr);

				else if (Key == "LightCullingMode")
				{
					if (ValueStr == "Off") Settings.LightCullingMode = ELightCullingMode::Off;
					else if (ValueStr == "Tiled") Settings.LightCullingMode = ELightCullingMode::Tiled;
				}
				else if (Key == "EnableTiledLightCulling" || Key == "EnableClusteredLightCulling")
				{
					Settings.LightCullingMode = std::stoi(ValueStr) != 0 ? ELightCullingMode::Tiled : ELightCullingMode::Off;
				}
				else if (Key == "LightingModel")
				{
					if (ValueStr == "Gouraud") Settings.LightingModel = ELightingModel::Gouraud;
					else if (ValueStr == "Lambert") Settings.LightingModel = ELightingModel::Lambert;
					else if (ValueStr == "Phong") Settings.LightingModel = ELightingModel::Phong;
					else if (ValueStr == "Unlit") Settings.LightingModel = ELightingModel::Unlit;
				}

				else if (Key == "CameraMoveSpeed") Settings.CameraSpeed = std::stof(ValueStr);
				else if (Key == "CameraRotateSensitivity") Settings.MouseSensitivity = std::stof(ValueStr);
				else if (Key == "GridSpacing") Settings.GridSpacing = std::stoi(ValueStr);
				else if (Key == "Horizontal") Settings.MultipleViewportsHorizontal = std::stof(ValueStr);
				else if (Key == "Vertical") Settings.MultipleViewportsVertical = std::stof(ValueStr);
				else if (Key == "Layout")
				{
					ELayoutMode Mode = ELayoutMode::QuadSplit;
					if (ParseLayoutModeName(ValueStr.c_str(), Mode))
						Settings.ViewportLayout = static_cast<int32>(Mode);
				}
				else if (Key == "SingleViewIndex")
				{
					const int32 ViewIndex = std::stoi(ValueStr);
					Settings.MultipleViewportsSingleViewIndex = ViewIndex >= 0 && ViewIndex < 4 ? ViewIndex : 0;
				}
				else if (Key == "DirectionalShadowMapSizeLevel")
				{
					Settings.DirectionalShadowMapSizeLevel = std::stoi(ValueStr);
				}
				else if (Key == "PointShadowMapSizeLevel")
				{
					Settings.PointShadowMapSizeLevel = std::stoi(ValueStr);
				}
				else if (Key == "SpotShadowMapSizeLevel")
				{
					Settings.SpotShadowMapSizeLevel = std::stoi(ValueStr);
				}
			}
		}
	}

	File.close();
	if (!std::isfinite(Settings.SceneDepthMaxRange))
		Settings.SceneDepthMaxRange = 50.0f;
	Settings.SceneDepthMaxRange = FMath::Clamp(Settings.SceneDepthMaxRange, 0.1f, 10000.0f);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Settings.bViewLocationSaved[Index] = LocationComponentMasks[Index] == 0x7;
		if (RotationComponentMasks[Index] == 0xF)
		{
			const FQuat& Rotation = Settings.ViewRotation[Index];
			const float LengthSquared = Rotation.X * Rotation.X + Rotation.Y * Rotation.Y +
				Rotation.Z * Rotation.Z + Rotation.W * Rotation.W;
			if (LengthSquared > 1.0e-12f)
			{
				Settings.ViewRotation[Index] = Rotation.Normalized();
				Settings.bViewRotationSaved[Index] = true;
			}
		}
	}
	Settings.DirectionalShadowMapSizeLevel = FMath::Clamp(Settings.DirectionalShadowMapSizeLevel, 0, 4);
	Settings.PointShadowMapSizeLevel = FMath::Clamp(Settings.PointShadowMapSizeLevel, 0, 4);
	Settings.SpotShadowMapSizeLevel = FMath::Clamp(Settings.SpotShadowMapSizeLevel, 0, 4);
	ApplyViewportSettings();
	return true;
}

// 뷰포트 설정 복사
void FEditorSettingsManager::ReadViewportSettings(FEditorSettings& Out) const
{
	if (!ViewportsPanel)
		return;

	for (int32 Index = 0; Index < 4; ++Index)
	{
		FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(Index);
		if (!Client)
			continue;

		Out.ViewPreset[Index] = static_cast<int32>(Client->GetViewportType());
		Out.ViewFov[Index] = Client->GetViewFOV();
		Out.ViewOrthoWidth[Index] = Client->GetOrthoWidth();
		Out.ViewMode[Index] = static_cast<int32>(Client->GetViewportMode());
		Out.ViewLocation[Index] = Client->GetViewLocation();
		Out.ViewRotation[Index] = Client->GetViewRotation().Quaternion();
	}
}

// 종료 시 설정 갱신
void FEditorSettingsManager::CaptureViewportSettings() { ReadViewportSettings(Settings); }

// 뷰포트 설정 적용
void FEditorSettingsManager::ApplyViewportSettings()
{
	if (!ViewportsPanel)
		return;
	ApplySceneDepthRange();

	for (int32 Index = 0; Index < 4; ++Index)
	{
		FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(Index);
		if (!Client)
			continue;

		if (Settings.ViewPreset[Index] >= 0 && Settings.ViewPreset[Index] < static_cast<int32>(ELevelViewportType::Count))
			Client->SetViewportType(static_cast<ELevelViewportType>(Settings.ViewPreset[Index]));
		if (Settings.ViewFov[Index] > 0.0f)
			Client->SetViewFOV(Settings.ViewFov[Index]);
		if (Settings.ViewOrthoWidth[Index] > 0.0f)
			Client->SetOrthoWidth(Settings.ViewOrthoWidth[Index]);
		if (Settings.bViewLocationSaved[Index]) Client->SetViewLocation(Settings.ViewLocation[Index]);
		if (Settings.bViewRotationSaved[Index]) Client->SetViewRotation(Settings.ViewRotation[Index].ToFRotator());
		if (Settings.ViewMode[Index] >= 0 && Settings.ViewMode[Index] < static_cast<int32>(EViewportMode::Count))
			Client->SetViewportMode(static_cast<EViewportMode>(Settings.ViewMode[Index]));
	}
}

void FEditorSettingsManager::ApplySceneDepthRange()
{
	if (!ViewportsPanel)
		return;

	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(Index))
			Client->SetMaxRange(Settings.SceneDepthMaxRange);
	}
}

