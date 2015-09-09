
#include "MQ2Navigation.h"

PreSetup("MQ2Navigation");
PLUGIN_VERSION(2.00);


#include <memory>

//----------------------------------------------------------------------------

std::unique_ptr<MQ2NavigationPlugin> g_mq2Nav;

//----------------------------------------------------------------------------

PLUGIN_API void InitializePlugin()
{
	DebugSpewAlways("Initializing MQ2Navigation");
	WriteChatf("\ay[MQ2Navigation]\ax v%1.2f by brainiac", MQ2Version);

	g_mq2Nav.reset(new MQ2NavigationPlugin);
}

PLUGIN_API void ShutdownPlugin()
{
	DebugSpewAlways("Shutting down MQ2Navigation");

	g_mq2Nav.reset();
}

PLUGIN_API void OnPulse()
{
	if (g_mq2Nav)
		g_mq2Nav->OnPulse();
}

PLUGIN_API void OnBeginZone()
{
	if (g_mq2Nav)
		g_mq2Nav->OnBeginZone();
}

PLUGIN_API void OnEndZone()
{
	if (g_mq2Nav)
		g_mq2Nav->OnEndZone();
}

PLUGIN_API void SetGameState(DWORD GameState)
{
	if (g_mq2Nav)
		g_mq2Nav->SetGameState(GameState);
}