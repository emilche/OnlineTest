project "Client-App"
   kind "ConsoleApp"
   language "C++"
   cppdialect "C++20"
   targetdir "Binaries/%{cfg.buildcfg}"
   staticruntime "off"

   files { "Source/**.h", "Source/**.cpp" }

   includedirs
   {
      "Source",

	  -- Include Core
	  "../Core/Source"
   }

   links
   {
      "Core"
   }

   targetdir ("../Binaries/" .. OutputDir .. "/%{prj.name}")
   objdir ("../Binaries/Intermediates/" .. OutputDir .. "/%{prj.name}")
   postbuildcommands 
	  {
	    '{COPY} "../%{OnlineBinDir}/GameNetworkingSockets.dll" "%{cfg.targetdir}"',
        '{COPY} "../%{OnlineBinDir}/libcrypto-3-x64.dll" "%{cfg.targetdir}"',
	    '{COPY} "../%{OnlineBinDir}/libprotobufd.dll" "%{cfg.targetdir}"',
	  }
   filter "system:windows"
       systemversion "latest"
       defines { "WINDOWS" }

   filter "configurations:Debug"
       defines { "DEBUG" }
       runtime "Debug"
       symbols "On"

   filter "configurations:Release"
       defines { "RELEASE" }
       runtime "Release"
       optimize "On"
       symbols "On"

   filter "configurations:Dist"
       defines { "DIST" }
       runtime "Release"
       optimize "On"
       symbols "Off"