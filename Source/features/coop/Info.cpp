#include "../info/ParamInfo.h"
#include "../../gui/HelpRegistry.h"

namespace gapfill::coop
{
namespace
{
    const info::ParamInfoRegistrar parameters
    {
        { "coopDeterministic", "Deterministic co-op", "Use stable musical slots to share fill ownership reproducibly", "", "" },
        { "coopDetGroup", "Deterministic group", "Choose A-H or use the existing co-op group without changing old automation", "", "" },
        { "coopSlot", "Fixed slot", "A unique slot identifies this track across playback and offline rendering", "1-32", "" },
        { "coopPriority", "Co-op priority", "Relative ownership share; zero keeps a small minimum weight", "0-100", "%" },
        { "coopYield", "Co-op yield", "Reduce fills while another track owns the slot; 100 means no natural fills", "0-100", "%" },
        { "coopGrid", "Co-op grid", "Musical ownership grid; the group's lowest fixed slot sets the shared grid", "", "" },
    };

    const HelpRegistrar help (6460, "Deterministic cooperation", [] (HelpWriter& w)
    {
        w.body ("DETERMINISTIC CO-OP adds reproducible ownership alongside the original Avoid overlap, Alternate and Follow modes. "
                "The original mode and group controls keep their old ranges and automation. Choose a deterministic group, or Use legacy group, "
                "then give every track a unique fixed slot. New duplicate slots are assigned a free slot; the assigned number is saved with the project.");
        w.body ("Priority controls relative ownership share. Yield reduces non-owner Density and Fill; at 100 only the owner may start natural fills. "
                "It never makes an unsafe gap eligible. A lone instance is unchanged. The lowest fixed slot chooses the shared grid; "
                "all tracks use the same version of membership at a future musical boundary. Changes may wait while another live track has not advanced. "
                "Without valid host transport cooperation pauses and ordinary timing remains available.");
        w.body ("GROUP RECALL schedules the selected scene index from each member's own bank at one common future PPQ boundary. "
                "Bank contents are not copied between tracks. A missing scene, late delivery, lost transport or seek cancels that receiver's request "
                "with a status message; it never applies late. Group cancellation, membership changes and participant teardown cancel outstanding requests. "
                "Panic keeps its existing dry-safe behavior and cancels pending group recalls.");
        w.body ("Cooperation works only between instances of the same loaded plugin format in one process. It uses no network, files or OS IPC. "
                "Sandboxed or bridged tracks may see only themselves; the status explains this. The registry supports 32 live instances. "
                "Original live modes retain their timing-dependent behavior. Save unique fixed slots before comparing offline and real-time renders.");
    });
}
}
