
WINDOWS_SDK_VERSION = "10.0.17763.0"

function AddGlobalSettings()
	language("C++")
	cppdialect("C++14")
	warnings("Extra")
	flags({
		"MultiProcessorCompile",
	})
end

function AddWindowsSettings()
	-- x86/x64
	filter({"platforms:x86 or x64"})
		systemversion(WINDOWS_SDK_VERSION)

	-- x86
	filter({"platforms:x86"})
		architecture("x86")
	-- x64
	filter({"platforms:x64"})
		architecture("x86_64")
	filter({})
end

function AddDebugSettings()
	filter({"configurations:Debug"})
		optimize("Off")
		symbols("On")
	filter({})
end

function AddDevelopSettings()
	filter({"configurations:Develop"})
		optimize("Speed")
		runtime("Debug")
		symbols("On")
	filter({})
end

function AddReleaseSettings()
	filter({"configurations:Release"})
		optimize("Full")
		symbols("Off")
		flags({
			"LinkTimeOptimization",
		})
	filter({})
end

workspace("saberGC")
	filename("saberGC." .. _ACTION)
	AddGlobalSettings()

	platforms({
		"x86",
		"x64",
	})
	AddWindowsSettings()

	configurations({
		"Debug",
		"Develop",
		"Release",
	})
	AddDebugSettings()
	AddDevelopSettings()
	AddReleaseSettings()

	project("saberGC")
		filename("saberGC." .. _ACTION)
		kind("ConsoleApp")
		targetdir("../bin")
		targetsuffix("_" .. _ACTION .. "_%{cfg.platform}_%{cfg.buildcfg}")
		objdir("../.intermediate." .. _ACTION .. "/%{prj.name}")

		includedirs({
			"../saberGC/include",
		})
		files({
			"../saberGC/**",
		})
		vpaths({
			{ ["*"] = { "../saberGC/**", } },
		})
