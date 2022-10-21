project "rml"
	libType()
	files { 
		"src/**.c",
		"src/**.cpp",
		"src/**.h",
		"genie.lua",
		"external/rml/Source/**.*",
		"external/rml/Include/**.*"
	}
	links { "engine", "renderer" }
	includedirs { "external/rml/Include", "../../external/freetype/include" }
	defines { "RMLUI_STATIC_LIB=1", "RMLUI_USE_CUSTOM_RTTI" }
	defaultConfigurations()
	useLua()

linkPlugin("rml")