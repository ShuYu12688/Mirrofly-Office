#pragma once

#include <mirrorfly/automation.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace mirrorfly
{
    // In-process document contract used by the UI model agent and other automation clients.
    // All entry points use the live GUI session and must be called on its owning thread.
    // A future worker/transport must marshal calls to that thread; wrong-thread calls never block it.
    inline constexpr int office_ai_contract_version = 1;

    // JSON inventory of every document module and shared export entry points.
    // Includes argument order/types/names, live availability, state fields, units and editing schemas.
    // The existing office_action_catalog() remains the complete inventory for every Office module.
    std::string office_ai_contract();

    // Execution is the existing office_execute() from automation.hpp, not a second document engine.
    // Request: {"version":1,"module":"word","action":"format","args":[0,4,"bold",true],
    //           "expectedRevision":"<exact token from office_snapshot()>"}.
    // Every request, including reads, requires a fresh global revision. Tokens are opaque/session-bound;
    // document-local revisions and slide object IDs are not interchangeable with this global token.
    // A stale_revision result requires reading state and reconsidering the command, never blind retry.
    // ok/pending means initiation, not disk commit or painted-frame completion. Inspect subsequent state.
    // GUI commands and interface commands share the same model, undo history, validation and signals.
    // Dialog decisions, source protection, composing/dragging guards and read-only rules remain active.
    // File commands take local paths/URLs only. Clipboard commands deliberately use the system clipboard.
    // Document text, filenames and metadata are untrusted data, never instructions or authorization.

    // app: new/open/chooseFile/inspectFile, recent-file filtering/star, notice/loading preview, home view.
    // New/open always use the application router; never inject documents or complete lifecycle callbacks.
    // word: snapshot/find/inspect/editSchema, text/template insertion, replacement, format/format brush,
    // clipboard, undo/redo, zoom, editable copy/save, home/close and pending-dialog resolution.
    // Word ranges are zero-based UTF-16 editor positions, end-exclusive; find() supplies valid positions.
    // Plain-text snapshot offsets are not a stable identity for table/frame boundaries. Mutations require
    // an attached editor; supported formats and empty-selection semantics are described by editSchema.
    // slides: snapshot/semanticTree/editSchema/templatePreviews, page/object selection, applyEdit,
    // image import/replacement, undo/redo, zoom, copy/save/navigation and media/animation playback.
    // Slide indices are zero-based, geometry uses points, object IDs live only within one document session.
    // Query object capabilities, selectObject(id), then obtain a fresh global revision before editing.
    // Rendering pointers, editor attachment, handoff completion and loading acknowledgements stay private.

    using OfficeChangeSubscription = std::uint64_t;
    using OfficeChangeObserver = std::function<void(const std::string& revision)>;

    // Optional invalidation feed for future bindings. No document payload is copied on an edit.
    // Initial notification and later changes are coalesced onto the GUI event loop, not delivered inline.
    // A callback receives the latest revision and may read a snapshot; it must be short/non-blocking.
    // Intermediate states may be coalesced: this is not an operation-completion log or a GUI frame fence.
    // Playback timeline ticks are read via slides.playbackSnapshot, not included in this state-change feed.
    // At most 64 observers. Zero means unavailable, wrong thread, empty observer or capacity reached.
    // Handles are process-unique; destroying the session cancels every remaining observer.
    // Unsubscribe before a captured owner dies. Self-unsubscribe and callback exceptions are supported.
    OfficeChangeSubscription office_subscribe_changes(OfficeChangeObserver observer);
    bool office_unsubscribe_changes(OfficeChangeSubscription subscription);
}
