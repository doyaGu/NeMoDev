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

#define EDITOR_VERSION 8u
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
    EDITOR_OBJECT_LIST = 4, EDITOR_INTEGER_LIST = 5, EDITOR_TEXT_LIST = 6, EDITOR_REAL_VALUE = 7
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
// Only the field matching type is read. Text and arrays are borrowed during a
// callback and copied on submission or sink delivery. Each request/result is
// limited to 1 MiB of text and 64 MiB of arrays. Object arrays do not retain objects.
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
} EditorValue;
typedef struct EditorScope {
    EditorDocument document;
    EditorView view;
} EditorScope;
typedef struct EditorRequest {
    EditorScope scope;
    const EditorValue *values;
    uint32_t count;
    EditorString handler; // empty selects the configured command/query
    EditorOperation operation; // assigned by the host, shared by synchronous nested commands
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
} EditorCommandDesc;
typedef enum EditorQueryRefresh { EDITOR_QUERY_DOCUMENT, EDITOR_QUERY_EACH_READ, EDITOR_QUERY_VERSIONED } EditorQueryRefresh;
// Reads a published version, without native access or mutations. A changed
// version invalidates only this query's stored values, independently of documents.
typedef uint64_t(EDITOR_CALL *EditorQueryVersion)(void *, const EditorHost *);
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
} EditorQueryDesc;
// A property connects a query field to a command argument. Text is the first
// value type with an editor; no native getter/setter escapes through this table.
typedef struct EditorPropertyDesc {
    uint32_t size;
    EditorString id, label, query, field, command, argument, editor;
} EditorPropertyDesc;
typedef enum EditorInputAction {
    EDITOR_INPUT_NONE = 0,
    EDITOR_INPUT_ACCEPT = 1,
    EDITOR_INPUT_CANCEL = 2,
    EDITOR_INPUT_ACTIVE = 4
} EditorInputAction;
typedef struct EditorChange {
    EditorScope scope;
    uint64_t revision;
} EditorChange;
typedef void(EDITOR_CALL *EditorChanged)(void *, const EditorHost *, const EditorChange *);
typedef struct EditorTextInput {
    uint32_t size;
    EditorEdit edit;
    EditorString label, value;
    uint32_t focus;
} EditorTextInput;
typedef uint32_t(EDITOR_CALL *EditorDrawText)(void *, const EditorHost *, const EditorTextInput *);
typedef void *(EDITOR_CALL *EditorCreateView)(void *, const EditorHost *, EditorView);
typedef void(EDITOR_CALL *EditorDestroyView)(void *);
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
typedef EditorStatus(EDITOR_CALL *EditorNavigateView)(void *, const EditorHost *, const EditorRequest *);
typedef enum EditorViewKind { EDITOR_WINDOW_VIEW, EDITOR_OVERLAY_VIEW } EditorViewKind;
typedef enum EditorInputKind { EDITOR_FILE_INPUT, EDITOR_SAVE_CHANGES_INPUT, EDITOR_CONFIRM_INPUT,
    EDITOR_OBJECT_EXPORT_INPUT, EDITOR_BEHAVIOR_EXPORT_INPUT,
    EDITOR_WEB_PAGE_INPUT, EDITOR_PRINT_INPUT } EditorInputKind;
// Drawing uses the separately versioned ImGui adapter. Implementations share
// the host's ImGui binary/context; editor interfaces include no ImGui headers.
typedef struct EditorDrawingVersion {
    uint32_t version, ioSize, styleSize, vertexSize, indexSize;
} EditorDrawingVersion;
typedef struct EditorTextEditorDesc {
    uint32_t size;
    EditorDrawingVersion imguiVersion;
    EditorString id, provider, title;
    EditorDrawText draw;
    void *data;
} EditorTextEditorDesc;
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
    // bounded sink as queries, but must not contain native object references.
    // The workspace retains single-instance state across close/reopen. Capture
    // and restore also precede replacing open providers; failure keeps old views.
    uint32_t stateVersion;
    EditorQuery saveState;
    EditorNavigateView restoreState;
    // Read-only instance data, distinct from saved navigation state. The
    // version is published by the view and contains no native object access.
    EditorQuery inspect;
    EditorQueryVersion inspectRevision;
    const EditorParameter *fields;
    uint32_t fieldCount;
} EditorViewDesc;
typedef struct EditorMenuDesc {
    EditorString id, action, location, label;
    int32_t order;
} EditorMenuDesc;
typedef struct EditorMenuLocation {
    EditorString id, title;
} EditorMenuLocation;
typedef struct EditorBindingDesc {
    EditorString action, chord;
} EditorBindingDesc;
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
    const EditorTextEditorDesc *editors;
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
    void(EDITOR_CALL *stop)(void *);
} EditorModuleDesc;
typedef EditorStatus(EDITOR_CALL *EditorModuleEntry)(const EditorHost *, EditorModuleDesc *);

// All calls are on the editor thread. Module descriptors and requests are
// copied at submission. Borrowed text/results live only inside their callback.
// View and edit IDs are never reused. Values do not retain native objects.
// Mutations requested inside drawing, query or notification callbacks run at
// the end of the frame, with provider and scope fixed at submission.
// Nested document edits form one undo step. Non-undoable document changes,
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
// A successful module entry transfers its data to the host until stop. On a
// failed entry the host also calls stop, if supplied, before unloading code.
// Exceptions must not leave module callbacks. The host also guards C++ throws.
struct EditorHost {
    uint32_t size, version;
    void *data;
    EditorStatus(EDITOR_CALL *query)(void *, EditorString, const EditorRequest *, EditorSink);
    EditorStatus(EDITOR_CALL *commandState)(void *, EditorString, const EditorRequest *);
    EditorStatus(EDITOR_CALL *canEdit)(void *, EditorString, EditorScope, EditorObject);
    EditorStatus(EDITOR_CALL *execute)(void *, EditorString, const EditorRequest *, EditorSink);
    EditorStatus(EDITOR_CALL *beginEdit)(void *, EditorString, EditorScope, EditorObject, EditorEdit *);
    EditorStatus(EDITOR_CALL *editValue)(void *, EditorEdit, EditorString);
    EditorStatus(EDITOR_CALL *finishEdit)(void *, EditorEdit, uint32_t commit);
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
    EditorStatus(EDITOR_CALL *queryRevision)(void *, EditorString, uint64_t *);
    // Tests and consumes a configured chord during drawing. Later global
    // shortcuts cannot dispatch the same key again in this frame.
    uint32_t(EDITOR_CALL *shortcut)(void *, EditorString action);
    EditorStatus(EDITOR_CALL *menu)(void *, EditorString location, EditorScope, EditorObject, EditorEdit *);

};

#ifdef __cplusplus
}
#endif
#endif
