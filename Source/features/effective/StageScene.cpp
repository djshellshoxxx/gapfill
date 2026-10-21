#include "Effective.h"
#include "../scenes/SceneOverlay.h"

namespace gapfill::Effective
{

// F-L07: while a recalled scene governs the decision PPQ its values replace the
// base values (macro-owned targets excepted). Identity when no overlay is set.
void applyScene (EngineSettings& s, double ppq, const TransportInfo& ts)
{
    const auto* view = s.perf.sceneView;

    if (view != nullptr && view->appliesAt (ppq, ts.valid))
        view->applyTo (s);
}

} // namespace gapfill::Effective
