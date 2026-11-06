#ifndef NEMODEV_EDITOR_H
#define NEMODEV_EDITOR_H

#include "editor/Identity.h"

#ifdef __cplusplus
extern "C" {
typedef vdev::ObjectRef EditorObject;
typedef vdev::DocumentToken EditorDocument;
#else
typedef ObjectRef EditorObject;
typedef DocumentToken EditorDocument;
#endif

#define EDITOR_VERSION 27u
#define EDITOR_TEXT(s) {s, sizeof(s) - 1u}
#if defined(_WIN32)
#define EDITOR_CALL __cdecl
#define EDITOR_MODULE_EXPORT __declspec(dllexport)
#else
#define EDITOR_CALL
#define EDITOR_MODULE_EXPORT __attribute__((visibility("default")))
#endif

typedef struct EditorString {
    const char *data;
    uint32_t size;
} EditorString;
typedef uint64_t EditorView;
typedef uint64_t EditorEdit;
typedef uint64_t EditorSubscription;
typedef uint64_t EditorOperation;
typedef enum EditorLogLevel {
    EDITOR_LOG_INFO,
    EDITOR_LOG_WARNING,
    EDITOR_LOG_ERROR
} EditorLogLevel;
typedef enum EditorStatus {
    EDITOR_OK,
    EDITOR_NO_CHANGE,
    EDITOR_REFUSED,
    EDITOR_STALE,
    EDITOR_CANCELLED,
    EDITOR_INVALID,
    EDITOR_ERROR,
    EDITOR_QUEUED
} EditorStatus;
typedef enum EditorValueType {
    EDITOR_OBJECT = 1, EDITOR_TEXT_VALUE = 2, EDITOR_INTEGER_VALUE = 3,
    EDITOR_OBJECT_LIST = 4, EDITOR_INTEGER_LIST = 5, EDITOR_TEXT_LIST = 6, EDITOR_REAL_VALUE = 7, EDITOR_REAL_LIST = 8,
    EDITOR_RECORD = 9
} EditorValueType;
typedef struct EditorObjects {
    const EditorObject *data;
    uint32_t count;
} EditorObjects;
typedef struct EditorIntegers {
    const uint64_t *data;
    uint32_t count;
} EditorIntegers;
typedef struct EditorStrings {
    const EditorString *data;
    uint32_t count;
} EditorStrings;
typedef struct EditorReals {
    const double *data;
    uint32_t count;
} EditorReals;
struct EditorValue;
typedef struct EditorFields {
    const struct EditorValue *data;
    uint32_t count;
} EditorFields;
// Only the field matching type is read. Text and arrays are borrowed during a
// callback and copied on submission or sink delivery. Each request/result is
// limited to 1 MiB of text and 64 MiB of arrays. Records have at most 64 levels,
// 4096 members per record and 65536 total fields; a request/result has at most
// 64 top-level fields. Object arrays do not retain objects.
typedef struct EditorValue {
    EditorString key;
    EditorValueType type;
    EditorObject object;
    EditorString text;
    uint64_t integer;
    EditorObjects objects;
    EditorIntegers integers;
    EditorStrings texts;
    double real; // finite only
    EditorReals reals; // finite components, in the property's declared order
    EditorFields fields; // named record members; copied with the containing value
} EditorValue;
typedef struct EditorScope {
    EditorDocument document;
    EditorView view;
} EditorScope;
typedef struct EditorRequest {
    EditorScope scope;
    // Values are identified by key. Their order follows the caller; omitted
    // optional values are appended by the host with their declared type.
    const EditorValue *values;
    uint32_t count;
    EditorString handler; // empty selects the configured command/query
    EditorOperation operation; // assigned by the host; nested document edits share their parent's completion
    // Optional preceding edit in this same document. Run only after it succeeds.
    // revisionArgument, when supplied, names an INTEGER command parameter that
    // receives that edit's resulting document revision. Both are copied on execute.
    EditorOperation afterEdit;
    EditorString revisionArgument;
} EditorRequest;
// Sinks copy their inputs before returning. They never take ownership.
// Command diagnostics use the text field "message" in their output sink.
// Refusing diagnostic delivery does not undo a successfully executed command.
typedef struct EditorSink {
    void *data;
    EditorStatus(EDITOR_CALL *write)(void *, const EditorValue *, uint32_t);
} EditorSink;
typedef struct EditorParameter {
    EditorString key;
    EditorValueType type;
    uint32_t optional; // omitted arguments use an empty value of their declared type
    uint32_t nullable; // OBJECT only: accepts an explicit null while remaining required
} EditorParameter;
typedef struct EditorHost EditorHost;
typedef EditorStatus(EDITOR_CALL *EditorCommand)(void *, const EditorHost *, const EditorRequest *, EditorSink);
typedef EditorStatus(EDITOR_CALL *EditorQuery)(void *, const EditorHost *, const EditorRequest *, EditorSink);
typedef EditorStatus(EDITOR_CALL *EditorCommandState)(void *, const EditorHost *, const EditorRequest *);
typedef enum EditorArea { EDITOR_DOCUMENT, EDITOR_WORKSPACE, EDITOR_APPLICATION } EditorArea;
typedef enum EditorExecutionState {
    EDITOR_EXECUTION_STOPPED = 0, EDITOR_EXECUTION_RUNNING = 1, EDITOR_EXECUTION_PAUSED = 2
} EditorExecutionState;
typedef enum EditorEffect {
    EDITOR_DOCUMENT_EDIT,
    EDITOR_DOCUMENT_CHANGE, // existing non-undoable changes manage their own history
    EDITOR_SESSION_CHANGE,
    EDITOR_HISTORY_CHANGE,
    EDITOR_FILE_OPERATION,
    EDITOR_RUNTIME_CONTROL
} EditorEffect;
// Optional live preview of a document edit. The host owns the interaction and
// keeps state between updates. CANCEL restores the original state, frees the
// provider's state and sets it to null. Without commitPreview, CANCEL also
// precedes run. With commitPreview, the existing preview is recorded once.
// No preview changes document history, Dirty or the document revision.
typedef enum EditorPreviewAction {
    EDITOR_PREVIEW_UPDATE,
    EDITOR_PREVIEW_CANCEL,
    // After commitPreview successfully records the live edit. Free state only.
    EDITOR_PREVIEW_RELEASE,
    // previewCommand dispatches this to the selected commitPreview callback.
    EDITOR_PREVIEW_COMMIT
} EditorPreviewAction;
typedef EditorStatus(EDITOR_CALL *EditorCommandPreview)(void *, const struct EditorHost *, const EditorRequest *,
                                                        EditorPreviewAction, void **state);
// Records an already previewed command in the host's document transaction.
// State remains owned by the host: CANCEL follows failure, RELEASE follows
// success. The host commits after RELEASE succeeds, so cleanup failure can
// still roll back the recorded change. Retain state until one of these actions.
typedef EditorStatus(EDITOR_CALL *EditorCommandPreviewCommit)(void *, const struct EditorHost *, const EditorRequest *,
                                                              void *state, EditorSink);
typedef struct EditorCommandDesc {
    uint32_t size;
    EditorString id, provider, title;
    const EditorParameter *parameters;
    uint32_t count;
    EditorCommand run;
    void *data;
    EditorArea area;
    EditorEffect effect;
    EditorCommandState state;
    EditorCommandPreview preview;
    EditorCommandPreviewCommit commitPreview;
    EditorString view; // optional view type; application callers use its last focused instance
} EditorCommandDesc;
typedef enum EditorQueryRefresh { EDITOR_QUERY_DOCUMENT, EDITOR_QUERY_EACH_READ, EDITOR_QUERY_VERSIONED } EditorQueryRefresh;
// Reads a published version, without native access or mutations. A changed
// version invalidates only this query's stored values, independently of documents.
typedef uint64_t(EDITOR_CALL *EditorQueryVersion)(void *, const EditorHost *);
typedef enum EditorQueryFlags {
    // A retained view state can contain deleted objects or identities from a
    // former document. The query must resolve each value before reading it.
    // The request itself still needs the active document and a live view.
    EDITOR_QUERY_RETAINED_REFERENCES = 1u << 0
} EditorQueryFlags;
typedef struct EditorQueryDesc {
    uint32_t size;
    EditorString id, provider;
    const EditorParameter *parameters;
    uint32_t count;
    EditorQuery read;
    void *data;
    EditorQueryRefresh refresh;
    EditorArea area;
    const EditorParameter *fields;
    uint32_t fieldCount;
    EditorQueryVersion revision; // required only for EDITOR_QUERY_VERSIONED
    uint32_t flags;              // zero keeps the ordinary live-object checks
} EditorQueryDesc;
// A property connects a typed query field to a command argument.
// Queries may receive additional position arguments (for example, table rows
// and a column). beginEdit/drawProperty receive the same query request.
// Remaining command arguments are captured from query fields with the same key
// and type (for example, the native type identity). No native pointers escape.
typedef struct EditorPropertyDesc {
    uint32_t size;
    EditorString id, label, query, field, command, argument, editor;
    EditorString choices; // optional read-only option query, requested only when its chooser opens
} EditorPropertyDesc;
typedef enum EditorInputAction {
    EDITOR_INPUT_NONE = 0,
    EDITOR_INPUT_ACCEPT = 1,
    EDITOR_INPUT_CANCEL = 2,
    EDITOR_INPUT_ACTIVE = 4,
    EDITOR_INPUT_CHANGED = 8,
    EDITOR_INPUT_INVALID = 16,
    EDITOR_INPUT_DEACTIVATED = 32
} EditorInputAction;
// Frame-coalesced changes include committed Script display edits, topology,
// Undo and Redo, regardless of whether a built-in or external command ran.
// Uncommitted gestures and successful rollback do not publish document edits.
// Subscribers read the resulting state; callbacks cannot mutate it immediately.
typedef struct EditorChange {
    EditorScope scope;
    uint64_t revision;
} EditorChange;
typedef void(EDITOR_CALL *EditorChanged)(void *, const EditorHost *, const EditorChange *);
typedef enum EditorValueStyle { EDITOR_VALUE_INLINE, EDITOR_VALUE_DIALOG } EditorValueStyle;
struct EditorValueInput;
typedef uint32_t(EDITOR_CALL *EditorDrawMembers)(void *, const EditorHost *, const struct EditorValueInput *);
// Fields carry the property query's copied metadata (options, display text and
// type identity). update copies one value of the declared type before returning.
// Dialogs edit their owned copies; a property draft commits through finishEdit.
// Built-ins use display (UTF-8), scale (real), and enum labels/values lists.
// Object choices are queried lazily through choicesQuery/choicesRequest and
// return labels plus full object references. Signed values use int64_t's bit
// representation in integer; CK integer parameters are validated as int32_t.
typedef struct EditorValueInput {
    uint32_t size;
    EditorEdit edit;
    EditorString label;
    EditorValue value;
    const EditorValue *fields;
    uint32_t count, focus;
    EditorSink update;
    EditorValueStyle style;
    float width, height;
    EditorString choicesQuery;
    EditorRequest choicesRequest;
    // Optional member presentation supplied by a working-copy owner. It uses
    // this input's update sink and reports the children's combined activity.
    // Container editors may reuse it or compose the described members directly.
    // The callback and data are borrowed only for this draw call.
    EditorDrawMembers drawMembers;
    void *membersData;
} EditorValueInput;
typedef uint32_t(EDITOR_CALL *EditorDrawValue)(void *, const EditorHost *, const EditorValueInput *);
typedef void *(EDITOR_CALL *EditorCreateView)(void *, const EditorHost *, EditorView);
typedef void(EDITOR_CALL *EditorDestroyView)(void *);
// Read-only close preparation, with the source scope still alive. Return OK or
// NO_CHANGE to allow a user close or provider replacement. Other results and
// exceptions refuse it before configuration is saved or any source is closed.
// Must not change edits, navigation or resources: another view or configuration
// save can still refuse the operation. Forced document close and shutdown skip it.
typedef EditorStatus(EDITOR_CALL *EditorCanCloseView)(void *, const EditorHost *, EditorView);
// Final cleanup for an actual close, after all fallible preparation and saving
// succeeded and before the source scope expires. May cancel edits and restore
// temporary selection. Must finish its cleanup without throwing; exceptions are
// reported, but cannot veto a close that has already committed. Destroy still
// runs. Never called when replacement preparation or configuration saving fails.
typedef void(EDITOR_CALL *EditorClosingView)(void *, const EditorHost *, EditorView);
// The workspace owns the window name and focus request. Window views use the
// drawing adapter to begin/end this window; overlays keep their own popups.
// A close request takes effect after drawing. The name is borrowed and NUL terminated.
typedef struct EditorViewFrame {
    EditorString window;
    uint32_t focus, close;
    // Existing workspace placement takes precedence over a view's initial size.
    uint32_t restorePlacement, dock;
    float x, y, width, height;
    uint32_t closable;
    // An unfinished edit/dialog that prevents replacing the document. Window
    // focus and read-only navigation do not make an interaction active.
    uint32_t active;
    uint32_t initialDock;
} EditorViewFrame;
typedef struct EditorPickRegion {
    EditorObject object;
    float left, top, right, bottom;
} EditorPickRegion;
typedef void(EDITOR_CALL *EditorDrawView)(void *, const EditorHost *, EditorView, EditorViewFrame *);
// A declared INTEGER navigation argument named "background" keeps window
// focus unchanged when nonzero; the workspace applies this to every view type.
typedef EditorStatus(EDITOR_CALL *EditorNavigateView)(void *, const EditorHost *, const EditorRequest *);
typedef enum EditorViewKind { EDITOR_WINDOW_VIEW, EDITOR_OVERLAY_VIEW } EditorViewKind;
typedef enum EditorInputKind { EDITOR_FILE_INPUT, EDITOR_SAVE_CHANGES_INPUT, EDITOR_CONFIRM_INPUT,
    EDITOR_OBJECT_EXPORT_INPUT, EDITOR_BEHAVIOR_EXPORT_INPUT,
    EDITOR_WEB_PAGE_INPUT, EDITOR_PRINT_INPUT, EDITOR_BEHAVIOR_INFORMATION_INPUT,
    EDITOR_DUPLICATE_NAMES_INPUT } EditorInputKind;
// Drawing uses the separately versioned ImGui adapter. Implementations share
// the host's ImGui binary/context; editor interfaces include no ImGui headers.
typedef struct EditorDrawingVersion {
    uint32_t version, ioSize, styleSize, vertexSize, indexSize;
} EditorDrawingVersion;
typedef struct EditorValueEditorDesc {
    uint32_t size;
    EditorDrawingVersion imguiVersion;
    EditorString id, provider, title;
    EditorDrawValue draw;
    void *data;
    EditorValueType type;
} EditorValueEditorDesc;
typedef struct EditorViewDesc {
    uint32_t size;
    EditorDrawingVersion imguiVersion;
    EditorString id, provider, title;
    EditorCreateView create;
    EditorDestroyView destroy;
    EditorDrawView draw;
    void *data;
    EditorViewKind kind;
    uint32_t singleInstance;
    EditorArea area; // only workspace/application views may draw while native work is active
    const EditorParameter *parameters; // navigation arguments
    uint32_t count;
    EditorNavigateView navigate;
    // State belongs to the view type and version, including across providers.
    // saveState/restoreState are supplied together. Saved values use the same
    // bounded sink as queries. Only documentBound views may save ObjectRefs.
    // The workspace retains document-independent single-instance state across close/reopen. Capture
    // and restore also precede replacing open providers; failure keeps old views.
    uint32_t stateVersion;
    EditorQuery saveState;
    EditorNavigateView restoreState;
    // Read-only instance data, distinct from saved navigation state. The
    // version is published by the view and contains no native object access.
    // An optional TEXT field named "title" supplies this instance's window title;
    // its workspace role or instance ID continues to identify the window.
    EditorQuery inspect;
    EditorQueryVersion inspectRevision;
    const EditorParameter *fields;
    uint32_t fieldCount;
    // Optional hit test in screen coordinates (REAL arguments "x", "y").
    // Object pickers also supply an optional INTEGER "class" filter.
    // Returns one OBJECT_LIST named "objects", ordered by pick priority.
    // The workspace selects this instance by window visibility, not title.
    // Empty means no hit; failure never falls through to another window.
    // Runs read-only on the editor thread; references belong to this scope.
    // When supplied, replaces this view's rectangular pick regions.
    EditorQuery pick;
    // The window belongs to this document and closes once replacement commits.
    // Its instance survives a failed replacement. Context-dependent resources
    // must be released/resumed through the module's document callback.
    // Its replacement state may contain checked ObjectRefs; that state is never
    // retained across documents. Other views save only document-independent values.
    uint32_t documentBound;
    EditorClosingView closing;
    // Named, document-independent fields from saveState that may be written to
    // user configuration. Restore overlays them on freshly captured state and
    // uses restoreState. The type's stateVersion also versions these settings.
    // Object references (including nested references) and documentBound views
    // are never persistent. Empty means window placement/open state only.
    const EditorParameter *settings;
    uint32_t settingCount;
    EditorString defaultTool; // optional registered tool for this view type
    EditorCanCloseView canClose;
} EditorViewDesc;
typedef struct EditorMenuDesc {
    EditorString id, action, location, label;
    int32_t order;
    EditorString checked; // optional no-argument query returning integer "checked"
} EditorMenuDesc;
typedef struct EditorMenuLocation {
    EditorString id, title;
} EditorMenuLocation;
typedef enum EditorInputPhase { EDITOR_KEY_PRESSED, EDITOR_KEY_DOWN, EDITOR_KEY_RELEASED } EditorInputPhase;

typedef struct EditorBindingDesc {
    EditorString action, chord;
    EditorString view;      // empty: application; otherwise only the focused view type
    EditorInputPhase phase; // pressed by default; shared by this action's chords
} EditorBindingDesc;
typedef enum EditorToolPhase {
    EDITOR_TOOL_ENTER,
    EDITOR_TOOL_FRAME,
    EDITOR_TOOL_CANCEL,
    EDITOR_TOOL_LEAVE
} EditorToolPhase;
typedef enum EditorToolFlags { EDITOR_TOOL_CAPTURED = 1u, EDITOR_TOOL_DEFAULT = 2u } EditorToolFlags;
// Pointer buttons: bit 0 left, bit 1 right, bit 2 middle. Coordinates are
// relative to the view's content; modifiers use Control/Alt/Shift/Super bits.
// Presses and wheel input start inside that view. A captured drag receives
// moves and releases until completion, focus loss, cancellation or closure.
typedef struct EditorToolInput {
    uint32_t size;
    EditorToolPhase phase;
    float x, y, dx, dy, wheel, wheelX;
    double time;
    uint32_t buttons, pressed, released, modifiers, hovered, focused;
} EditorToolInput;
// State belongs to the receiving view. Use beginCommand/editValue/finishEdit
// for document gestures. ENTER selects it, FRAME supplies input, CANCEL
// restores live edits, LEAVE ends its use. LEAVE may accept an interaction;
// the host cancels any preview left unfinished. Failures also cancel previews.
// Selecting a tool is a session command and never adds an Undo step by itself.
typedef EditorStatus(EDITOR_CALL *EditorToolInputHandler)(void *, const EditorHost *, const EditorRequest *,
                                                          const EditorToolInput *, uint32_t *flags);
typedef struct EditorToolDesc {
    uint32_t size;
    EditorString id, provider, title, view;
    EditorToolInputHandler input;
    void *data;
    EditorCommandState state;
} EditorToolDesc;
typedef enum EditorToolbarFlags { EDITOR_TOOLBAR_RIGHT = 1u, EDITOR_TOOLBAR_STRETCH = 2u } EditorToolbarFlags;
typedef struct EditorToolbarFrame {
    uint32_t size;
    float left, top, width, height, scale;
    uint32_t enabled, checked;
    EditorString action, title, shortcut, tooltip;
} EditorToolbarFrame;
// A custom control owns its widget IDs and may submit commands through the host. Returning nonzero
// activates the item's configured action with its copied arguments.
typedef uint32_t(EDITOR_CALL *EditorToolbarDraw)(void *, const EditorHost *, const EditorRequest *,
                                                 const EditorToolbarFrame *);
typedef struct EditorToolbarItemDesc {
    uint32_t size;
    EditorDrawingVersion imguiVersion;
    EditorString id, provider, location, title, action, checked;
    const EditorValue *arguments;
    uint32_t count;
    float x, y, width, height;
    uint32_t flags;
    EditorToolbarDraw draw;
    void *data;
} EditorToolbarItemDesc;
typedef struct EditorToolbarLocation {
    EditorString id, title, view;
} EditorToolbarLocation;
// Theme values use named roles: color.* is 0xRRGGBBAA, metric.* is a
// finite REAL or pair of REALs, and font.*/image.* refer to resource IDs.
// A parent names another provider of this same theme ID. Values override
// the parent's roles without changing their types; inheritance is acyclic.
typedef struct EditorThemeDesc {
    uint32_t size;
    EditorString id, provider, title, parent;
    const EditorValue *values;
    uint32_t count;
} EditorThemeDesc;
// Ordered font files: the first provides the face, later files fill missing
// glyphs. An empty first file requests the renderer's built-in monospace face.
// Paths are UTF-8, absolute or relative to the contributing module library.
typedef struct EditorFontDesc {
    uint32_t size;
    EditorString id, provider, title;
    const EditorString *files;
    uint32_t count;
    float height;
} EditorFontDesc;
// Complete top-left RGBA8 pixels, copied on registration. Image and font
// declarations own no native resource; the UI owns their rendered resources.
typedef struct EditorImageDesc {
    uint32_t size;
    EditorString id, provider, title;
    uint32_t width, height;
    const unsigned char *pixels;
    uint32_t byteCount;
} EditorImageDesc;
// Document-owned binary data. Reference slots, including null slots, keep
// their order across saves and imports. Bytes must not contain native IDs.
// One record per (data id, owner); zero owner denotes the composition.
// A record is bounded to 16 MiB and 65536 reference slots; a document to
// 4096 records and 64 MiB including references. Zero-length bytes are valid.
typedef struct EditorDocumentData {
    uint32_t size, version;
    EditorObject owner;
    const unsigned char *bytes;
    uint32_t byteCount;
    EditorObjects objects;
} EditorDocumentData;
typedef struct EditorDocumentDataSink {
    void *data;
    EditorStatus(EDITOR_CALL *write)(void *, const EditorDocumentData *);
} EditorDocumentDataSink;
// Pure data callbacks: called by the serialized file task, possibly off the
// editor thread. Do not call EditorHost, retain borrowed spans or touch UI/CK.
// Write exactly one result at the declared current version, retaining owner.
// A failure/exception aborts the load and restores the original document.
typedef EditorStatus(EDITOR_CALL *EditorUpgradeDocumentData)(void *, const EditorDocumentData *, EditorDocumentDataSink);
typedef EditorStatus(EDITOR_CALL *EditorMergeDocumentData)(void *, const EditorDocumentData *,
                                                         const EditorDocumentData *, EditorDocumentDataSink);
typedef struct EditorDocumentDataDesc {
    uint32_t size;
    EditorString id, provider;
    uint32_t version;
    EditorUpgradeDocumentData upgrade;
    EditorMergeDocumentData merge;
    void *data;
} EditorDocumentDataDesc;
// Missing/newer/unsupported schemas are preserved unchanged and read-only.
// Older schemas are upgraded once on load when upgrade is supplied. Without
// merge, differing records with the same id and owner fail Import/Merge;
// identical records and different owners need no callback. Unused descriptors
// create no file data. Provider selection uses kind "document-data".
// Pending work is prepared before menus; presentation state is published just
// before views draw; interactive work advances after them. Callbacks do not
// render UI or present a GPU frame.
typedef enum EditorModulePhase { EDITOR_PREPARE_FRAME, EDITOR_BEFORE_VIEWS, EDITOR_AFTER_VIEWS } EditorModulePhase;
typedef enum EditorDocumentPhase {
    EDITOR_DOCUMENT_DETACH,
    EDITOR_DOCUMENT_RESTORE,
    EDITOR_DOCUMENT_REPLACED
} EditorDocumentPhase;
typedef struct EditorDocumentChange {
    uint32_t size;
    EditorDocumentPhase phase;
    EditorDocument document;
    uint64_t revision;
    uint32_t replaceContext;
} EditorDocumentChange;
typedef struct EditorModuleDesc {
    uint32_t size, version;
    EditorString id;
    const EditorString *dependencies;
    uint32_t dependencyCount;
    const EditorCommandDesc *commands;
    uint32_t commandCount;
    const EditorQueryDesc *queries;
    uint32_t queryCount;
    const EditorPropertyDesc *properties;
    uint32_t propertyCount;
    const EditorValueEditorDesc *editors;
    uint32_t editorCount;
    const EditorViewDesc *views;
    uint32_t viewCount;
    const EditorMenuDesc *menus;
    uint32_t menuCount;
    const EditorBindingDesc *bindings;
    uint32_t bindingCount;
    const EditorMenuLocation *locations;
    uint32_t locationCount;
    void *data;
    // Releases entry-owned data, including entries that were never started.
    // All module data is released before any module library is unloaded.
    void(EDITOR_CALL *destroy)(void *);
    const EditorToolDesc *tools;
    uint32_t toolCount;
    const EditorToolbarItemDesc *toolbarItems;
    uint32_t toolbarItemCount;
    const EditorToolbarLocation *toolbars;
    uint32_t toolbarCount;
    const EditorThemeDesc *themes;
    uint32_t themeCount;
    const EditorFontDesc *fonts;
    uint32_t fontCount;
    const EditorImageDesc *images;
    uint32_t imageCount;
    const EditorDocumentDataDesc *documentData;
    uint32_t documentDataCount;
    // Called once for enabled modules in dependency order, after activation
    // and core document creation. Startup may query but cannot submit commands.
    // OK/NO_CHANGE succeeds; failure stops the application module batch.
    EditorStatus(EDITOR_CALL *start)(void *, const EditorHost *);
    // Editor thread only; configuration and recursive lifecycle calls are
    // refused. All phases also run for modules with no start callback.
    void(EDITOR_CALL *update)(void *, const EditorHost *, EditorModulePhase);
    // Ends activity while views, published data and the native context remain
    // valid. Called once for every attempted start, in reverse dependency order.
    // It cannot veto shutdown or submit new commands; accepted operations may
    // still complete and edits may be cancelled. Data stays alive until destroy.
    void(EDITOR_CALL *stop)(void *, const EditorHost *);
    // UI thread, between native tasks. DETACH releases context-dependent
    // resources while retaining instance state. RESTORE resumes the same
    // document after failure; REPLACED receives the new document after commit.
    // Detach visits reverse dependency order, recovery visits forward order.
    // Callbacks may query and release their own resources, but cannot mutate
    // documents/configuration or submit commands. OK/NO_CHANGE succeeds.
    // A failed DETACH is also given RESTORE, since it may have released resources.
    EditorStatus(EDITOR_CALL *document)(void *, const EditorHost *, const EditorDocumentChange *);
    // Orders startup, frame callbacks and shutdown when the named module is
    // present. Missing or disabled optional dependencies are allowed.
    const EditorString *optionalDependencies;
    uint32_t optionalDependencyCount;
} EditorModuleDesc;
typedef EditorStatus(EDITOR_CALL *EditorModuleEntry)(const EditorHost *, EditorModuleDesc *);

// All calls are on the editor thread. Module descriptors and requests are
// copied at submission. Borrowed text/results live only inside their callback.
// View and edit IDs are never reused. Values do not retain native objects.
// Mutations requested inside drawing, query or notification callbacks run at
// the end of the frame, with provider and scope fixed at submission.
// Nested document edits form one undo step. A session action that starts a
// document edit receives a separate document command completion number. Non-undoable document changes,
// file, history, session and runtime
// commands keep their own semantics and cannot run inside a document edit.
// Command state is read-only and is checked again before dispatch. Queued file
// work returns an operation ID; its owner retains completion and side effects
// even after the requesting view closes.
// Accepted calls return "operation" as an integer output, including calls
// deferred to the frame boundary. application.operation reads their final status;
// the last 256 completed calls are retained. Document edits finish at commit;
// other asynchronous providers finish through completeOperation on this thread.
// Once a nested provider accepts asynchronous work, its owner finishes the
// operation. An outer callback failure is kept with the actual output result.
// A successful module entry transfers its data to the host until destroy. On a
// failed entry the host also calls destroy, if supplied, before unloading code.
// Exceptions must not leave module callbacks. The host also guards C++ throws.
struct EditorHost {
    uint32_t size, version;
    void *data;
    EditorStatus(EDITOR_CALL *query)(void *, EditorString, const EditorRequest *, EditorSink);
    EditorStatus(EDITOR_CALL *commandState)(void *, EditorString, const EditorRequest *);
    EditorStatus(EDITOR_CALL *canEdit)(void *, EditorString, EditorScope, EditorObject);
    EditorStatus(EDITOR_CALL *execute)(void *, EditorString, const EditorRequest *, EditorSink);
    EditorStatus(EDITOR_CALL *beginEdit)(void *, EditorString, const EditorRequest *, EditorEdit *);
    EditorStatus(EDITOR_CALL *editValue)(void *, EditorEdit, const EditorValue *);
    // On a queued commit, operation receives the command completion ID. Null
    // is allowed; cancellation and failure set a supplied output to zero.
    EditorStatus(EDITOR_CALL *finishEdit)(void *, EditorEdit, uint32_t commit, EditorOperation *operation);
    uint32_t(EDITOR_CALL *drawEdit)(void *, EditorEdit, EditorString label, uint32_t focus);
    EditorStatus(EDITOR_CALL *watch)(void *, EditorView, EditorChanged, void *, EditorSubscription *);
    void(EDITOR_CALL *unwatch)(void *, EditorSubscription);
    EditorScope(EDITOR_CALL *scope)(void *, EditorView);
    uint64_t(EDITOR_CALL *revision)(void *);
    EditorObject(EDITOR_CALL *selection)(void *);
    EditorStatus(EDITOR_CALL *completeOperation)(void *, EditorOperation, EditorStatus,
                                                const EditorValue *, uint32_t count);
    // Called from a registered command, outside document edits and
    // drawing/query/notification callbacks.
    // instance=0 uses an existing instance of the selected type, or opens one.
    // Otherwise it must name a live instance of type. Navigation is validated
    // against that view's parameters. Failure closes a newly created instance;
    // existing views are responsible for leaving their navigation unchanged.
    // Success returns the instance in the integer field "view" and requests focus.
    EditorStatus(EDITOR_CALL *navigateView)(void *, EditorString type, EditorView instance,
                                           const EditorRequest *, EditorSink);
    EditorStatus(EDITOR_CALL *queryView)(void *, EditorView, EditorSink);
    EditorStatus(EDITOR_CALL *pickRegions)(void *, EditorView, const EditorPickRegion *, uint32_t);
    // A cheap published query version. Arguments remain validated when the
    // projection is read; views invalidate their local parameters separately.
    // Compare for equality only. A provider change also changes this version;
    // it is independent of revision fields in the returned data.
    EditorStatus(EDITOR_CALL *queryRevision)(void *, EditorString, uint64_t *);
    // Tests and consumes a configured chord during drawing. Later global
    // shortcuts cannot dispatch the same key again in this frame.
    uint32_t(EDITOR_CALL *shortcut)(void *, EditorString action);
    // Starts a continuous command with captured arguments and a typed "value".
    // editValue/finishEdit drive it; closing its scope cancels its live state.
    EditorStatus(EDITOR_CALL *beginCommand)(void *, EditorString, const EditorRequest *, EditorEdit *);
    // Delegation by command callbacks, including an explicitly named provider.
    // Only callable within an existing preview or its committing command.
    EditorStatus(EDITOR_CALL *previewCommand)(void *, EditorString, const EditorRequest *, EditorPreviewAction,
                                              void **state, EditorSink);
    EditorStatus(EDITOR_CALL *menu)(void *, EditorString location, EditorScope, EditorObject, EditorEdit *);
    // Inline property input: begin on interaction, retain the typed draft,
    // finish once on accept/deactivation, and cancel on Escape. edit is owned
    // by this view and is cleared only when that interaction has ended.
    uint32_t(EDITOR_CALL *drawProperty)(void *, EditorString property, const EditorRequest *,
                                      EditorEdit *, EditorString label);
    // Compose value editors without starting another document edit. Child
    // updates go to the containing value's sink. Provider choices are retained
    // by the containing interaction; recursion is bounded by the host.
    uint32_t(EDITOR_CALL *drawValue)(void *, EditorString editor, const EditorValueInput *);

    // View-local tool selection also works while restoring that same view's
    // state; it never edits the document or changes another view.
    // Editor-thread view tools. Tool IDs are also ordinary commands, selected
    // through the same provider settings. Each view keeps its own selection.
    // toolbar/shortcuts/inputBinding are available during drawing only.
    EditorStatus(EDITOR_CALL *selectTool)(void *, EditorScope, EditorString);
    EditorString(EDITOR_CALL *activeTool)(void *, EditorView);
    EditorStatus(EDITOR_CALL *toolInput)(void *, EditorScope, const EditorToolInput *);
    void(EDITOR_CALL *cancelTool)(void *, EditorView);
    EditorStatus(EDITOR_CALL *toolbar)(void *, EditorString, EditorScope, const EditorToolbarFrame *);
    void(EDITOR_CALL *shortcuts)(void *, EditorScope);
    uint32_t(EDITOR_CALL *inputBinding)(void *, EditorScope, EditorString action, EditorInputPhase, uint32_t consume);
    // Read copies through sink; a missing record returns NO_CHANGE. Writes
    // are allowed only inside a registered document editing command, and join
    // its undo step. Null value removes the owner's record. Unsupported schema
    // versions cannot be overwritten or removed. Cancellation restores data.
    EditorStatus(EDITOR_CALL *readDocumentData)(void *, EditorScope, EditorString id, EditorObject owner,
                                               EditorDocumentDataSink);
    EditorStatus(EDITOR_CALL *writeDocumentData)(void *, EditorScope, EditorString id, EditorObject owner,
                                                const EditorDocumentData *value);
};

#ifdef __cplusplus
}
#endif
#endif
