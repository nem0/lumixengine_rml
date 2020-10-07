#include "editor/studio_app.h"
#include "editor/world_editor.h"

namespace Lumix {

LUMIX_STUDIO_ENTRY(rml) {
	app.registerComponent("", "rml_canvas", "RML / Canvas");
	
	WorldEditor& editor = app.getWorldEditor();
	return nullptr;
}

} // namespace Lumix
