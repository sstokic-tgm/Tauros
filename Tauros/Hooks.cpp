#include "Hooks.hpp"
#include "MaterialHelper.hpp"
#include "Options.hpp"
#include "Utils.hpp"
#include "XorStr.hpp"
#include "VFTableHook.hpp"

#include "DrawManager.hpp"
#include "ImGUI/imgui.h"
#include "ImGUI/DX9/imgui_impl_dx9.h"

#include "GUI.hpp"

#include "Hacks/AutoAccept.hpp"
#include "Hacks/Bhop.hpp"
#include "Hacks/Chams.hpp"
#include "Hacks/ESP.hpp"
#include "Hacks/VisualMisc.hpp"
#include "Hacks/RCS.hpp"
#include "Hacks/SkinChanger.hpp"
#include "Hacks/Trigger.hpp"
#include "Hacks/Aim.hpp"

using namespace std;

extern LRESULT ImGui_ImplDX9_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern HMODULE g_hLib;
extern HWND g_hWnd;

namespace Hooks
{
    unique_ptr<VFTableHook>            g_pD3DDevice9Hook = nullptr;
	unique_ptr<VFTableHook>            g_pClientHook = nullptr;
    unique_ptr<VFTableHook>            g_pClientModeHook = nullptr;
    unique_ptr<VFTableHook>            g_pMatSurfaceHook = nullptr;
	unique_ptr<VFTableHook>            g_pVGUIPanelHook = nullptr;
	unique_ptr<VFTableHook>            g_pModelRenderHook = nullptr;
	unique_ptr<VFTableHook>            g_pDrawModelExecuteHook = nullptr;

    unique_ptr<DrawManager>            g_pRenderer = nullptr;

    EndScene_t                         g_fnOriginalEndScene = nullptr;
    Reset_t                            g_fnOriginalReset = nullptr;
    CreateMove_t                       g_fnOriginalCreateMove = nullptr;
	PlaySound_t                        g_fnOriginalPlaySound = nullptr;
	PaintTraverse_t                    g_fnOriginalPaintTraverse = nullptr;	
	FrameStageNotify_t                 g_fnOriginalFrameStageNotify = nullptr;
	OverrideView_t                     g_fnOriginalOverrideView = nullptr;
	DrawModelExecute_t                 g_fnOriginalDrawModelExecute = nullptr;
	OverrideMouseInput_t               g_fnOriginalOverrideMouseInput = nullptr;

	SetClanTag_t                       g_fnSetClanTag = nullptr;

    WNDPROC                            g_pOldWindowProc = nullptr; //Old WNDPROC pointer

	GUI                                g_Gui;

    bool                               g_vecPressedKeys[256] = {};
    bool                               g_bWasInitialized = false;

	void Initialize()
    {
		//Find CSGO main window
		while (!g_hWnd)
		{
			g_hWnd = FindWindowA(XorStr("Valve001"), nullptr);
			Sleep(200);
		}

        NetvarManager::Instance()->CreateDatabase();
        NetvarManager::Instance()->Dump(Utils::GetDllDir() + XorStr("netvar_dump.txt"));

        auto dwDevice = **reinterpret_cast<uint32_t**>(Utils::FindSignature(XorStr("shaderapidx9.dll"), XorStr("A1 ? ? ? ? 6A 00 53")) + 0x1);
		g_fnSetClanTag = reinterpret_cast<SetClanTag_t>(Utils::FindSignature(XorStr("engine.dll"), XorStr("53 56 57 8B DA 8B F9 FF 15")));

        g_pD3DDevice9Hook = make_unique<VFTableHook>(reinterpret_cast<PPDWORD>(dwDevice), true);
		g_pClientHook = make_unique<VFTableHook>(reinterpret_cast<PPDWORD>(se::Interfaces::Client()), true);
        g_pClientModeHook = make_unique<VFTableHook>(reinterpret_cast<PPDWORD>(se::Interfaces::ClientMode()), true);
        g_pMatSurfaceHook = make_unique<VFTableHook>(reinterpret_cast<PPDWORD>(se::Interfaces::MatSurface()), true);
	    g_pVGUIPanelHook = make_unique<VFTableHook>(reinterpret_cast<PPDWORD>(se::Interfaces::VGUIPanel()), true);
		g_pModelRenderHook = make_unique<VFTableHook>(reinterpret_cast<PPDWORD>(se::Interfaces::ModelRender()), true);

        //Replace the WindowProc with our own to capture user input
        g_pOldWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Hooked_WndProc)));

        g_fnOriginalReset = g_pD3DDevice9Hook->Hook(16, Hooked_Reset);                                                                   // IDirect3DDevice9::Reset
        g_fnOriginalEndScene = g_pD3DDevice9Hook->Hook(42, Hooked_EndScene);                                                             // IDirect3DDevice9::EndScene
		g_fnOriginalFrameStageNotify = g_pClientHook->Hook(36, reinterpret_cast<FrameStageNotify_t>(Hooked_FrameStageNotify));           // Client::FrameStageNotify
		g_fnOriginalOverrideView = g_pClientModeHook->Hook(18, reinterpret_cast<OverrideView_t>(Hooked_OverrideView));                   // IClientMode::OverrideView
        g_fnOriginalCreateMove = g_pClientModeHook->Hook(24, reinterpret_cast<CreateMove_t>(Hooked_CreateMove));                         // IClientMode::CreateMove
		g_fnOriginalPlaySound = g_pMatSurfaceHook->Hook(82, reinterpret_cast<PlaySound_t>(Hooked_PlaySound));                            // ISurface::PlaySound
	    g_fnOriginalPaintTraverse = g_pVGUIPanelHook->Hook(41, reinterpret_cast<PaintTraverse_t>(Hooked_PaintTraverse));                 // IPanel::PaintTraverse
		g_fnOriginalDrawModelExecute = g_pModelRenderHook->Hook(21, reinterpret_cast<DrawModelExecute_t>(Hooked_DrawModelExecute));      // IVModelRender::DrawModelExecute
		g_fnOriginalOverrideMouseInput = g_pClientModeHook->Hook(23, reinterpret_cast<OverrideMouseInput_t>(Hooked_OverrideMouseInput)); // IClientMode::OverrideMouseInput
    }

    void Restore()
    {
        //Restore the WindowProc
        SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_pOldWindowProc));

        g_pRenderer->InvalidateObjects();

        //Remove the hooks
        g_pD3DDevice9Hook->RestoreTable();
		g_pClientHook->RestoreTable();
        g_pClientModeHook->RestoreTable();
        g_pMatSurfaceHook->RestoreTable();
		g_pVGUIPanelHook->RestoreTable();
		g_pModelRenderHook->RestoreTable();
    }

    void GUI_Init(IDirect3DDevice9* pDevice)
    {
        //Initializes the GUI and the renderer
        ImGui_ImplDX9_Init(g_hWnd, pDevice);
		g_Gui = GUI(pDevice);
        g_pRenderer = make_unique<DrawManager>(pDevice);
        g_pRenderer->CreateObjects();
        g_bWasInitialized = true;
    }

	HRESULT __stdcall Hooked_EndScene(IDirect3DDevice9* pDevice)
	{
		using namespace se;
		if (!g_bWasInitialized)
		{
			GUI_Init(pDevice);
		}
		else
		{
			g_Gui.UpdateCursorVisibility();
			ImGui_ImplDX9_NewFrame();
			g_Gui.Show();

			g_pRenderer->BeginRendering();
			ImGui::Render();
			g_pRenderer->EndRendering();
		}

		return g_fnOriginalEndScene(pDevice);
	}

	HRESULT __stdcall Hooked_Reset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters)
	{
		if (!g_bWasInitialized)
			return g_fnOriginalReset(pDevice, pPresentationParameters);

		// Device is on LOST state.
		ImGui_ImplDX9_InvalidateDeviceObjects();
		g_pRenderer->InvalidateObjects();

		// Call original Reset.
		auto hrOriginalReset = g_fnOriginalReset(pDevice, pPresentationParameters);

		g_pRenderer->CreateObjects();
		ImGui_ImplDX9_CreateDeviceObjects();
		return hrOriginalReset;
	}

    void __fastcall Hooked_PaintTraverse(se::IPanel* pPanel, void * edx, se::VPANEL vguiPanel, bool forceRepaint, bool allowForce)
    {
	    g_fnOriginalPaintTraverse(pPanel, vguiPanel, forceRepaint, allowForce);
		if (Options::g_bCleanScreenshot && se::Interfaces::Engine()->IsTakingScreenshot())
			return;

		using namespace se;
		static unsigned int overlayPanel = 0;
		if (overlayPanel == 0)
		{
			if (!strstr(Interfaces::VGUIPanel()->GetName(vguiPanel), XorStr("MatSystemTopPanel")))
				return;

			overlayPanel = vguiPanel;
		}

		if (overlayPanel != vguiPanel)
			return;

		if (!Interfaces::Engine()->IsInGame())
			return;

		ESP::PaintTraverse_Post();
	}

    LRESULT __stdcall Hooked_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch(uMsg)
		{
            case WM_LBUTTONDOWN:
                g_vecPressedKeys[VK_LBUTTON] = true;
                break;
            case WM_LBUTTONUP:
                g_vecPressedKeys[VK_LBUTTON] = false;
                break;
            case WM_RBUTTONDOWN:
                g_vecPressedKeys[VK_RBUTTON] = true;
                break;
            case WM_RBUTTONUP:
                g_vecPressedKeys[VK_RBUTTON] = false;
                break;
            case WM_KEYDOWN:
                g_vecPressedKeys[wParam] = true;
                break;
            case WM_KEYUP:
                g_vecPressedKeys[wParam] = false;
                break;
            default: break;
        }

		g_Gui.CheckToggle(g_vecPressedKeys, VK_INSERT);

        if(g_bWasInitialized && Options::g_bMainWindowOpen && ImGui_ImplDX9_WndProcHandler(hWnd, uMsg, wParam, lParam))
            return true;

        return CallWindowProc(g_pOldWindowProc, hWnd, uMsg, wParam, lParam);
    }

    bool __stdcall Hooked_CreateMove(float sample_input_frametime, se::CUserCmd* pCmd)
    {
	    auto bRet = g_fnOriginalCreateMove(se::Interfaces::ClientMode(), sample_input_frametime, pCmd);
        auto pLocal = C_CSPlayer::GetLocalPlayer();

		Bhop::CreateMove_Post(pLocal, pCmd);
		bRet &= RCS::CreateMove_Post(pLocal, pCmd);
		Trigger::CreateMove_Post(pLocal, pCmd);
		Aim::CreateMove_Post(pLocal, pCmd);

		g_fnSetClanTag("Cerberus", "Cerberus");

        return bRet;
    }

	void __fastcall Hooked_FrameStageNotify(void* ecx, void* edx, se::ClientFrameStage_t stage)
    {
		NoSmoke::FrameStageNotify_Pre(stage);
		NoFlash::FrameStageNotify_Pre(stage);
		RCS::FrameStageNotify_Pre(stage);
		SkinChanger::FrameStageNotify_Pre(stage);

		g_fnOriginalFrameStageNotify(ecx, stage);

		RCS::FrameStageNotify_Post(stage);
    }
	
	void __fastcall Hooked_OverrideView(void* ecx, void* edx, se::CViewSetup* pViewSetup)
	{
		RCS::OverrideView_Pre(pViewSetup);

		g_fnOriginalOverrideView(ecx, pViewSetup);
	}

	void __fastcall Hooked_DrawModelExecute(void* ecx, void* edx, se::IMatRenderContext* ctx, const se::DrawModelState_t &state, const se::ModelRenderInfo_t &pInfo, se::matrix3x4_t *pCustomBoneToWorld)
	{
		g_pModelRenderHook->Unhook(21); // Prevent infinite recursive loop
		MatHelper.Initialize();

		if (!Options::g_bCleanScreenshot || !se::Interfaces::Engine()->IsTakingScreenshot())
		{
			Chams::DrawModelExecute_Pre(ecx, ctx, state, pInfo, pCustomBoneToWorld);
			Hands::DrawModelExecute_Pre(ecx, ctx, state, pInfo, pCustomBoneToWorld);
		}
		
		g_fnOriginalDrawModelExecute(ecx, ctx, state, pInfo, pCustomBoneToWorld);
		se::Interfaces::ModelRender()->ForcedMaterialOverride(nullptr);
		g_pModelRenderHook->Hook(21, reinterpret_cast<DrawModelExecute_t>(Hooked_DrawModelExecute));
	}

	void __stdcall Hooked_OverrideMouseInput(float* x, float* y)
	{
		using namespace se;
		g_fnOriginalOverrideMouseInput(Interfaces::ClientMode(), x, y);

		Aim::OverrideMouseInput_Post(x, y);
	}

	void __stdcall Hooked_PlaySound(const char* szFileName)
	{
		g_fnOriginalPlaySound(se::Interfaces::MatSurface(), szFileName);

		AutoAccept::PlaySound_Post(szFileName);
	}
}