#include "EnginePCH.h"
#include "SplashScreen.h"

#include "Render/ImageLoader.h"
#include "Core/Misc/Paths.h"
#include "Core/Misc/Version.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <cmath>

namespace
{
	constexpr int32 BaseSplashWidth = 720;
	constexpr int32 BaseImageHeight = 300;
	constexpr const wchar_t* SplashClassName = L"KurisuSplash";
	constexpr int32 BaseFooterHeight = 70;
	constexpr int32 BaseProgressBarHeight = 1;
	constexpr int32 BaseTextMargin = 10;
	constexpr int32 BaseEmblemSize = 40;
	constexpr COLORREF FooterBgColor = RGB(0, 0, 0);
	constexpr COLORREF AccentColor = RGB(102, 128, 112);
	constexpr COLORREF AccentTrackColor = RGB(54, 54, 58);
	constexpr COLORREF BorderColor = RGB(116, 116, 120);

	struct FSplashState
	{
		std::thread Thread;
		std::mutex Mutex;
		std::condition_variable WindowReady;
		bool bWindowCreated = false;

		HINSTANCE hInstance = nullptr;
		HWND hWnd = nullptr;
		HBITMAP Bitmap = nullptr;
		HBITMAP LogoBitmap = nullptr;
		int32 Width = 0;
		int32 Height = 0;
		int32 FooterHeight = BaseFooterHeight;
		int32 ImageAreaHeight = 0;
		int32 ImageWidth = 0;
		int32 ImageHeight = 0;
		int32 LogoWidth = 0;
		int32 LogoHeight = 0;

		std::wstring Title;
		std::wstring Text;
		float Progress = 0.0f;
	};

	float CalcSplashUiScale(int32 Width)
	{
		return static_cast<float>(Width) / BaseSplashWidth;
	}

	int32 ScaleSplashPx(int32 BasePx, float UiScale)
	{
		return std::max(1, static_cast<int32>(std::lround(static_cast<float>(BasePx) * UiScale)));
	}

	FSplashState GSplash;

	HBITMAP CreateBitmapFromImage(const FImageData& Image, COLORREF BgColor = FooterBgColor)
	{
		BITMAPINFO Info{};
		Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		Info.bmiHeader.biWidth = static_cast<LONG>(Image.Width);
		Info.bmiHeader.biHeight = -static_cast<LONG>(Image.Height);   // 음수 = 위에서 아래로 (stb 순서)
		Info.bmiHeader.biPlanes = 1;
		Info.bmiHeader.biBitCount = 32;
		Info.bmiHeader.biCompression = BI_RGB;

		void* Bits = nullptr;
		HBITMAP Bitmap = CreateDIBSection(nullptr, &Info, DIB_RGB_COLORS, &Bits, nullptr, 0);
		if (!Bitmap)
		{
			return nullptr;
		}

		const uint8 BgR = GetRValue(BgColor);
		const uint8 BgG = GetGValue(BgColor);
		const uint8 BgB = GetBValue(BgColor);

		const uint8* Src = Image.Pixels.GetData();
		uint8* Dst = static_cast<uint8*>(Bits);
		const uint32 PixelCount = Image.Width * Image.Height;
		for (uint32 i = 0; i < PixelCount; ++i)
		{
			const uint8 A = Src[i * 4 + 3];
			if (A == 255)
			{
				Dst[i * 4 + 0] = Src[i * 4 + 2];
				Dst[i * 4 + 1] = Src[i * 4 + 1];
				Dst[i * 4 + 2] = Src[i * 4 + 0];
			}
			else if (A == 0)
			{
				Dst[i * 4 + 0] = BgB;
				Dst[i * 4 + 1] = BgG;
				Dst[i * 4 + 2] = BgR;
			}
			else
			{
				const uint32 InvA = 255 - A;
				Dst[i * 4 + 0] = static_cast<uint8>((Src[i * 4 + 2] * A + BgB * InvA) / 255);
				Dst[i * 4 + 1] = static_cast<uint8>((Src[i * 4 + 1] * A + BgG * InvA) / 255);
				Dst[i * 4 + 2] = static_cast<uint8>((Src[i * 4 + 0] * A + BgR * InvA) / 255);
			}
			Dst[i * 4 + 3] = 255;
		}

		return Bitmap;
	}

	HFONT CreateSplashFont(int32 PixelHeight, int32 Weight)
	{
		return CreateFontW(
			-PixelHeight, 0, 0, 0, Weight, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
	}

	void Paint(HWND hWnd)
	{
		PAINTSTRUCT Ps;
		HDC ScreenDC = BeginPaint(hWnd, &Ps);

		RECT Client;
		GetClientRect(hWnd, &Client);
		const int32 W = Client.right;
		const int32 H = Client.bottom;
		const float UiScale = CalcSplashUiScale(W);
		const int32 FooterH = (GSplash.FooterHeight > 0) ? GSplash.FooterHeight : ScaleSplashPx(BaseFooterHeight, UiScale);
		const int32 ProgressBarH = ScaleSplashPx(BaseProgressBarHeight, UiScale);
		const int32 Margin = ScaleSplashPx(BaseTextMargin, UiScale);
		const int32 EmblemPx = ScaleSplashPx(BaseEmblemSize, UiScale);
		const int32 ImageH = (GSplash.ImageAreaHeight > 0) ? GSplash.ImageAreaHeight : std::max(0, H - FooterH);

		HDC MemDC = CreateCompatibleDC(ScreenDC);
		HBITMAP BackBuffer = CreateCompatibleBitmap(ScreenDC, W, H);
		HGDIOBJ OldBackBuffer = SelectObject(MemDC, BackBuffer);

		// 전체 배경(검은 푸터 포함)을 먼저 채운다.
		HBRUSH FooterBrush = CreateSolidBrush(FooterBgColor);
		FillRect(MemDC, &Client, FooterBrush);
		DeleteObject(FooterBrush);

		if (GSplash.Bitmap && ImageH > 0)
		{
			HDC ImageDC = CreateCompatibleDC(ScreenDC);
			HGDIOBJ OldImage = SelectObject(ImageDC, GSplash.Bitmap);

			SetStretchBltMode(MemDC, HALFTONE);
			SetBrushOrgEx(MemDC, 0, 0, nullptr);
			// Fill the wide artwork area without distorting the image.
			const float ImageScale = std::max(static_cast<float>(W) / GSplash.ImageWidth,
				static_cast<float>(ImageH) / GSplash.ImageHeight);
			const int32 SourceW = std::max(1, static_cast<int32>(W / ImageScale));
			const int32 SourceH = std::max(1, static_cast<int32>(ImageH / ImageScale));
			StretchBlt(MemDC, 0, 0, W, ImageH, ImageDC,
				(GSplash.ImageWidth - SourceW) / 2, (GSplash.ImageHeight - SourceH) / 3,
				SourceW, SourceH, SRCCOPY);

			SelectObject(ImageDC, OldImage);
			DeleteDC(ImageDC);
		}

		std::wstring Text;
		float Progress;
		{
			std::lock_guard Lock(GSplash.Mutex);
			Text = GSplash.Text;
			Progress = GSplash.Progress;
		}

		const int32 Percent = static_cast<int32>(std::lround(FMath::Clamp(Progress, 0.0f, 1.0f) * 100.0f));
		const std::wstring StatusLine = Text.empty() ? L"Loading..." :
			std::to_wstring(Percent) + L"% - " + Text;

		// 오른쪽 엠블럼 및 저작권 텍스트 영역
		int32 RightReserve = Margin;
		if (GSplash.LogoBitmap)
		{
			HDC LogoDC = CreateCompatibleDC(ScreenDC);
			HGDIOBJ OldLogo = SelectObject(LogoDC, GSplash.LogoBitmap);

			const int32 LogoX = W - Margin - EmblemPx;
			const int32 LogoY = ImageH + ScaleSplashPx(9, UiScale);
			SetStretchBltMode(MemDC, HALFTONE);
			SetBrushOrgEx(MemDC, 0, 0, nullptr);
			StretchBlt(MemDC, LogoX, LogoY, EmblemPx, EmblemPx,
				LogoDC, 0, 0, GSplash.LogoWidth, GSplash.LogoHeight, SRCCOPY);

			SelectObject(LogoDC, OldLogo);
			DeleteDC(LogoDC);
			RightReserve += EmblemPx + ScaleSplashPx(18, UiScale);
		}

		SetBkMode(MemDC, TRANSPARENT);

		HFONT TitleFont = CreateSplashFont(ScaleSplashPx(18, UiScale), FW_NORMAL);
		HFONT SubFont = CreateSplashFont(ScaleSplashPx(12, UiScale), FW_NORMAL);
		HFONT StatusFont = CreateSplashFont(ScaleSplashPx(11, UiScale), FW_NORMAL);
		HFONT CopyFont = CreateSplashFont(ScaleSplashPx(11, UiScale), FW_NORMAL);
		HGDIOBJ OldFont = SelectObject(MemDC, TitleFont);

		const int32 TextRight = std::max(Margin + ScaleSplashPx(40, UiScale), W - std::max(RightReserve, ScaleSplashPx(150, UiScale)));

		// 엔진 이름
		SetTextColor(MemDC, RGB(245, 245, 245));
		RECT TitleRect = { Margin, ImageH + ScaleSplashPx(6, UiScale), TextRight, ImageH + ScaleSplashPx(28, UiScale) };
		DrawTextW(MemDC, GSplash.Title.c_str(), -1, &TitleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

		// 부제
		SelectObject(MemDC, SubFont);
		SetTextColor(MemDC, RGB(160, 160, 160));
		RECT SubRect = { Margin, ImageH + ScaleSplashPx(27, UiScale), TextRight, ImageH + ScaleSplashPx(43, UiScale) };
		DrawTextW(MemDC, L"Kurisu Editor " KURISU_VERSION_STRING, -1, &SubRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

		// % 및 현재 로딩 중인 파일/단계 상태
		SelectObject(MemDC, CopyFont);
		SetTextColor(MemDC, RGB(150, 150, 150));
		constexpr const wchar_t* Copyright = L"Copyright © Kurisu Engine";
		SIZE CopySize{};
		GetTextExtentPoint32W(MemDC, Copyright, lstrlenW(Copyright), &CopySize);
		const int32 CopyLeft = W - Margin - CopySize.cx;
		RECT CopyRect = { CopyLeft, ImageH + ScaleSplashPx(50, UiScale), W - Margin, H - ScaleSplashPx(3, UiScale) };
		DrawTextW(MemDC, Copyright, -1, &CopyRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

		SelectObject(MemDC, StatusFont);
		SetTextColor(MemDC, RGB(160, 160, 160));
		RECT StatusRect = { Margin, CopyRect.top, CopyLeft - Margin, CopyRect.bottom };
		DrawTextW(MemDC, StatusLine.c_str(), -1, &StatusRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

		SelectObject(MemDC, OldFont);
		DeleteObject(TitleFont);
		DeleteObject(SubFont);
		DeleteObject(StatusFont);
		DeleteObject(CopyFont);

		// Thin progress accent along the top edge.
		RECT BarBack = { 0, 1, W, 1 + ProgressBarH };
		HBRUSH BackBrush = CreateSolidBrush(AccentTrackColor);
		FillRect(MemDC, &BarBack, BackBrush);
		DeleteObject(BackBrush);

		const LONG FillWidth = static_cast<LONG>(W * FMath::Clamp(Progress, 0.0f, 1.0f));
		if (FillWidth > 0)
		{
			RECT BarFill = { 0, 1, FillWidth, 1 + ProgressBarH };
			HBRUSH FillBrush = CreateSolidBrush(AccentColor);
			FillRect(MemDC, &BarFill, FillBrush);
			DeleteObject(FillBrush);
		}

		// 창 외곽선
		HPEN FramePen = CreatePen(PS_SOLID, 1, BorderColor);
		HGDIOBJ OldPen = SelectObject(MemDC, FramePen);
		HGDIOBJ OldBrush = SelectObject(MemDC, GetStockObject(NULL_BRUSH));
		const int32 Corner = ScaleSplashPx(12, UiScale);
		RoundRect(MemDC, 0, 0, W, H, Corner, Corner);
		SelectObject(MemDC, OldBrush);
		SelectObject(MemDC, OldPen);
		DeleteObject(FramePen);

		BitBlt(ScreenDC, 0, 0, W, H, MemDC, 0, 0, SRCCOPY);

		SelectObject(MemDC, OldBackBuffer);
		DeleteObject(BackBuffer);
		DeleteDC(MemDC);
		EndPaint(hWnd, &Ps);
	}

	LRESULT CALLBACK SplashWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
	{
		switch (Msg)
		{
		case WM_PAINT:
			Paint(hWnd);
			return 0;
		case WM_ERASEBKGND:
			return 1;
		case WM_CLOSE:
			DestroyWindow(hWnd);
			return 0;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		}

		return DefWindowProcW(hWnd, Msg, wParam, lParam);
	}

	void SplashThreadMain()
	{
		WNDCLASSEXW Wc{};
		Wc.cbSize = sizeof(WNDCLASSEXW);
		Wc.lpfnWndProc = SplashWndProc;
		Wc.hInstance = GSplash.hInstance;
		Wc.hCursor = LoadCursorW(nullptr, IDC_APPSTARTING);
		Wc.lpszClassName = SplashClassName;

		RegisterClassExW(&Wc);

		const int32 X = (GetSystemMetrics(SM_CXSCREEN) - GSplash.Width) / 2;
		const int32 Y = (GetSystemMetrics(SM_CYSCREEN) - GSplash.Height) / 2;

		HWND hWnd = CreateWindowExW(
			WS_EX_TOOLWINDOW,                   // 작업 표시줄에 안 뜸 (다른 창에 가려질 수 있음)
			SplashClassName, L"", WS_POPUP,     // 테두리·제목 표시줄 없음
			X, Y, GSplash.Width, GSplash.Height,
			nullptr, nullptr, GSplash.hInstance, nullptr);

		{
			std::lock_guard Lock(GSplash.Mutex);
			GSplash.hWnd = hWnd;
			GSplash.bWindowCreated = true;
		}
		GSplash.WindowReady.notify_one();

		if (!hWnd) return;

		const int32 Corner = ScaleSplashPx(12, CalcSplashUiScale(GSplash.Width));
		HRGN Region = CreateRoundRectRgn(0, 0, GSplash.Width + 1, GSplash.Height + 1, Corner, Corner);
		if (Region && !SetWindowRgn(hWnd, Region, FALSE)) DeleteObject(Region);
		ShowWindow(hWnd, SW_SHOW);
		UpdateWindow(hWnd);

		MSG Msg;
		while (GetMessageW(&Msg, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&Msg);
			DispatchMessageW(&Msg);
		}

		UnregisterClassW(SplashClassName, GSplash.hInstance);

	}
}




void FSplashScreen::Show(HINSTANCE hInstance, const FString& ImagePath)
{
	if (GSplash.Thread.joinable())
	{
		return;
	}

	// 스플래시 이미지는 엔진 콘텐츠("Engine/Splash/...")에 있다.
	const FImageData Image = ImageLoader::Load(FPaths::ResolveContentPath(ImagePath));
	if (Image.IsValid())
	{
		GSplash.Bitmap = CreateBitmapFromImage(Image);
		GSplash.ImageWidth = static_cast<int32>(Image.Width);
		GSplash.ImageHeight = static_cast<int32>(Image.Height);

	}

	// Match the reference proportions, independent of artwork resolution.
	const float DpiScale = static_cast<float>(GetDpiForSystem()) / 96.0f;
	const float FitScale = std::min(
		GetSystemMetrics(SM_CXSCREEN) * 0.9f / BaseSplashWidth,
		GetSystemMetrics(SM_CYSCREEN) * 0.9f / (BaseImageHeight + BaseFooterHeight));
	const float UiScale = std::min(DpiScale, FitScale);
	GSplash.Width = ScaleSplashPx(BaseSplashWidth, UiScale);
	GSplash.ImageAreaHeight = ScaleSplashPx(BaseImageHeight, UiScale);
	GSplash.FooterHeight = ScaleSplashPx(BaseFooterHeight, UiScale);
	GSplash.Height = GSplash.ImageAreaHeight + GSplash.FooterHeight;
	GSplash.Title = L"Kurisu Editor";
	if (FPaths::HasProject())
	{
		GSplash.Title += L" - " + FPaths::FromUtf8(FPaths::ProjectName()).wstring();
	}

	const FImageData LogoImage = ImageLoader::Load(FPaths::ResolveContentPath("Engine/Icons/KurisuLogoReverse.png"));
	if (LogoImage.IsValid())
	{
		GSplash.LogoBitmap = CreateBitmapFromImage(LogoImage, FooterBgColor);
		GSplash.LogoWidth = static_cast<int32>(LogoImage.Width);
		GSplash.LogoHeight = static_cast<int32>(LogoImage.Height);
	}

	GSplash.hInstance = hInstance;
	GSplash.bWindowCreated = false;
	GSplash.Thread = std::thread(SplashThreadMain);

	std::unique_lock Lock(GSplash.Mutex);
	GSplash.WindowReady.wait(Lock, [] {return GSplash.bWindowCreated;});
}

void FSplashScreen::SetText(const FString& Text)
{
	if (Text.empty())
	{
		SetText(std::wstring());
		return;
	}

	const int32 Needed = MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int32>(Text.size()), nullptr, 0);
	if (Needed > 0)
	{
		std::wstring WString(Needed, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int32>(Text.size()), WString.data(), Needed);
		SetText(WString);
		return;
	}

	std::wstring Fallback(Text.begin(), Text.end());
	SetText(Fallback);
}

void FSplashScreen::SetText(const std::wstring& Text)
{
	HWND hWnd;
	{
		std::lock_guard Lock(GSplash.Mutex);
		GSplash.Text = Text;
		hWnd = GSplash.hWnd;
	}

	if (hWnd)
	{
		InvalidateRect(hWnd, nullptr, FALSE);
	}
}

void FSplashScreen::SetProgress(float Ratio)
{
	HWND hWnd;
	{
		std::lock_guard Lock(GSplash.Mutex);
		GSplash.Progress = FMath::Clamp(Ratio, 0.0f, 1.0f);
		hWnd = GSplash.hWnd;
	}
	if (hWnd)
	{
		InvalidateRect(hWnd, nullptr, FALSE);
	}
}

void FSplashScreen::Hide()
{
	if (!GSplash.Thread.joinable())
	{
		return;
	}

	HWND hWnd;
	{
		std::lock_guard Lock(GSplash.Mutex);
		hWnd = GSplash.hWnd;
		GSplash.hWnd = nullptr;   // 이후 SetText가 닫힌 창을 건드리지 않게
	}
	if (hWnd)
	{
		PostMessageW(hWnd, WM_CLOSE, 0, 0);   // 창은 만든 스레드만 파괴할 수 있으므로 메시지로 요청한다
	}
	GSplash.Thread.join();

	if (GSplash.Bitmap)
	{
		DeleteObject(GSplash.Bitmap);
		GSplash.Bitmap = nullptr;
	}
	if (GSplash.LogoBitmap)
	{
		DeleteObject(GSplash.LogoBitmap);
		GSplash.LogoBitmap = nullptr;
	}
	GSplash.ImageAreaHeight = 0;
	GSplash.LogoWidth = 0;
	GSplash.LogoHeight = 0;
	GSplash.Text.clear();
	GSplash.Progress = 0.0f;
}
