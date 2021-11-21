
function AddGlobalSettings()
	language("C++")
	cppdialect("C++17")
--	exceptionhandling("Off")  -- if desired
--	rtti("Off")               -- if desired
	warnings("Extra")
	flags({
		"MultiProcessorCompile",
	})
end

function AddWindowsSettings()
	-- x86/x64
	filter({"platforms:x86 or x64"})

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
		defines({
			"_DEBUG",
		})
		optimize("Off")
		symbols("On")
	filter({})
end

function AddDevelopSettings()
	filter({"configurations:Develop"})
		defines({
			"NDEBUG",
		})
		optimize("Speed")
		runtime("Debug")
		symbols("On")
	filter({})
end

function AddReleaseSettings()
	filter({"configurations:Release"})
		defines({
			"NDEBUG",
		})
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
