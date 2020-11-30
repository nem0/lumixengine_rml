#include "editor/studio_app.h"
#include "editor/world_editor.h"

namespace Lumix {

LUMIX_STUDIO_ENTRY(rml) {
	WorldEditor& editor = app.getWorldEditor();
	return nullptr;
}

} // namespace Lumix
