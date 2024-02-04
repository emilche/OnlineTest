workspace "EmilOnlineTest"
   architecture "x64"
   configurations { "Debug", "Release", "Dist" }
   startproject "Client-App"

   -- Workspace-wide build options for MSVC
   filter "system:windows"
      buildoptions { "/EHsc", "/Zc:preprocessor", "/Zc:__cplusplus" }

OutputDir = "%{cfg.system}-%{cfg.architecture}/%{cfg.buildcfg}"
OnlineBinDir = "Core/vendor/GameNetworkingSockets/bin/%{cfg.system}/%{cfg.buildcfg}/"

group "Core"
	include "Core/Build-Core.lua"
group ""

include "Client-App/Build-App.lua"
include "Server-App/Build-App.lua"