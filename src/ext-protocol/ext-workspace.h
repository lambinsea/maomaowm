#include "../common/transaction.h"
#include "../common/util.h"
#include "ext-workspace-v1-protocol.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#define EXT_WORKSPACE_V1_VERSION 1
#define WS_STATE_INVALID 0xffffffff

/*
 * Main workspace manager structure that tracks all workspace groups and
 * workspaces This is the root object that clients interact with
 */
struct dwl_ext_workspace_manager {
	struct wl_global *global;  // Wayland global object for this protocol
	struct wl_list groups;	   // List of all workspace groups
	struct wl_list workspaces; // List of all workspaces across all groups
	uint32_t caps; // Capability flags for what operations are allowed
	struct wl_event_source *idle_source; // For deferring done events
	struct wl_event_loop *event_loop;	 // Reference to event loop

	/* Event listeners */
	struct {
		struct wl_listener display_destroy; // Cleanup when display is destroyed
	} on;

	struct wl_list resources; // List of client resources bound to this manager
};

/*
 * Workspace group - typically corresponds to a monitor/output
 * Contains multiple workspaces that can be shown on that output
 */
struct dwl_ext_workspace_group {
	struct dwl_ext_workspace_manager *manager; // Parent manager
	uint32_t capabilities;					   // Group-specific capabilities
	/* Group events */
	struct {
		struct wl_signal
			create_workspace;	  // Emitted when client requests new workspace
		struct wl_signal destroy; // Emitted when group is destroyed
	} events;

	struct wl_list link; // Link in manager's groups list
	struct wl_list
		outputs; // Outputs (monitors) in this group (group_output structs)
	struct wl_list resources; // Client resources bound to this group
};

/*
 * Individual workspace that can contain windows
 * Belongs to a group and can be activated/shown on its outputs
 */
struct dwl_ext_workspace {
	struct dwl_ext_workspace_manager *manager; // Parent manager
	struct dwl_ext_workspace_group *group;	   // Parent group (may be NULL)
	char *id;								   // Unique identifier string
	char *name;								   // Human-readable name
	struct wl_array coordinates; // Spatial coordinates (for tiling/positioning)
	uint32_t capabilities;		 // Workspace-specific capabilities
	uint32_t state;				 // Current state (active, urgent, etc)
	uint32_t state_pending; // Pending state changes (batched before commit)

	/* Workspace events */
	struct {
		struct wl_signal activate;	 // Emitted when workspace is activated
		struct wl_signal deactivate; // Emitted when workspace is deactivated
		struct wl_signal remove;	 // Emitted when workspace is removed
		struct wl_signal assign;	 // Emitted when assigned to new group
		struct wl_signal destroy;	 // Emitted when workspace is destroyed
	} events;

	struct wl_list link;	  // Link in manager's workspaces list
	struct wl_list resources; // Client resources bound to this workspace
};

/* Capability flags for workspace groups and workspaces */
enum dwl_ext_workspace_caps {
	WS_CAP_NONE = 0,
	WS_CAP_GRP_ALL = 0x0000ffff, // Mask for all group capabilities
	WS_CAP_WS_ALL = 0xffff0000,	 // Mask for all workspace capabilities

	WS_CAP_GRP_WS_CREATE = 1 << 0, // Group can create workspaces

	WS_CAP_WS_ACTIVATE = 1 << 16,	// Workspace can be activated
	WS_CAP_WS_DEACTIVATE = 1 << 17, // Workspace can be deactivated
	WS_CAP_WS_REMOVE = 1 << 18,		// Workspace can be removed
	WS_CAP_WS_ASSIGN = 1 << 19,		// Workspace can be assigned to group
};

/* Pending workspace change flags (used in transactions) */
enum pending_ext_workspaces_change {
	WS_PENDING_WS_CREATE = 1 << 0, // Workspace creation pending

	WS_PENDING_WS_ACTIVATE = 1 << 1,   // Activation pending
	WS_PENDING_WS_DEACTIVATE = 1 << 2, // Deactivation pending
	WS_PENDING_WS_REMOVE = 1 << 3,	   // Removal pending
	WS_PENDING_WS_ASSIGN = 1 << 4,	   // Assignment pending
};

/*
 * Tracks an output's membership in a workspace group
 * Links wlr_output to dwl_ext_workspace_group
 */
struct group_output {
	struct wlr_output *wlr_output;		   // Reference to wlroots output
	struct dwl_ext_workspace_group *group; // Parent workspace group
	/* Event listeners */
	struct {
		struct wl_listener group_destroy; // Cleanup when group is destroyed
		struct wl_listener output_bind;	  // Handle new client binding to output
		struct wl_listener output_destroy; // Cleanup when output is destroyed
	} on;

	struct wl_list link; // Link in group's outputs list
};

/* Forward declaration for dwl's Monitor type */
typedef struct Monitor Monitor;

/*
 * Combines dwl's internal workspace state with protocol objects
 * Links tag-based workspaces to wayland protocol objects
 */
struct workspace {
	struct wl_list link; // Link in global workspaces list
	unsigned int tag;	 // Numeric identifier (1-9, 0=overview)
	Monitor *m;			 // Associated monitor
	struct dwl_ext_workspace *ext_workspace; // Protocol object
	/* Event listeners */
	struct wl_listener activate;
	struct wl_listener deactivate;
	struct wl_listener assign;
	struct wl_listener remove;
};

/* Event data for workspace creation requests */
struct ws_create_workspace_event {
	char *name; // Requested workspace name
	/* Transaction operation tracking */
	struct {
		struct wl_listener transaction_op_destroy;
	} on;
};

/* Global manager instance */
struct dwl_ext_workspace_manager *ext_manager;
/* Global list of all workspaces */
struct wl_list workspaces;

/* Function prototypes */
struct dwl_ext_workspace_manager *
dwl_ext_workspace_manager_create(struct wl_display *display, uint32_t caps,
								 uint32_t version);

struct dwl_ext_workspace_group *
dwl_ext_workspace_group_create(struct dwl_ext_workspace_manager *manager);

void dwl_ext_workspace_group_output_enter(struct dwl_ext_workspace_group *group,
										  struct wlr_output *output);

void dwl_ext_workspace_group_output_leave(struct dwl_ext_workspace_group *group,
										  struct wlr_output *output);

void dwl_ext_workspace_group_destroy(struct dwl_ext_workspace_group *group);

struct dwl_ext_workspace *
dwl_ext_workspace_create(struct dwl_ext_workspace_manager *manager,
						 const char *id);

void dwl_ext_workspace_assign_to_group(struct dwl_ext_workspace *workspace,
									   struct dwl_ext_workspace_group *group);

void dwl_ext_workspace_set_name(struct dwl_ext_workspace *workspace,
								const char *name);

void dwl_ext_workspace_set_active(struct dwl_ext_workspace *workspace,
								  bool enabled);

void dwl_ext_workspace_set_urgent(struct dwl_ext_workspace *workspace,
								  bool enabled);

void dwl_ext_workspace_set_hidden(struct dwl_ext_workspace *workspace,
								  bool enabled);

void dwl_ext_workspace_set_coordinates(struct dwl_ext_workspace *workspace,
									   struct wl_array *coordinates);

void dwl_ext_workspace_destroy(struct dwl_ext_workspace *workspace);

void ext_group_output_send_initial_state(struct dwl_ext_workspace_group *group,
										 struct wl_resource *group_resource);

void ext_manager_schedule_done_event(struct dwl_ext_workspace_manager *manager);

/* Workspace protocol implementation */

/*
 * Handle client request to destroy workspace resource
 * Just destroys the client's resource, not the actual workspace
 */
static void workspace_handle_destroy(struct wl_client *client,
									 struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

/*
 * Handle client request to activate workspace
 * Adds to transaction queue for atomic application
 */
static void workspace_handle_activate(struct wl_client *client,
									  struct wl_resource *resource) {
	struct dwl_wl_resource_addon *addon = wl_resource_get_user_data(resource);
	if (!addon) {
		return;
	}
	struct dwl_ext_workspace *workspace = addon->data;
	dwl_transaction_op_add(addon->ctx, WS_PENDING_WS_ACTIVATE, workspace, NULL);
}

/*
 * Handle client request to deactivate workspace
 * Adds to transaction queue for atomic application
 */
static void workspace_handle_deactivate(struct wl_client *client,
										struct wl_resource *resource) {
	struct dwl_wl_resource_addon *addon = wl_resource_get_user_data(resource);
	if (!addon) {
		return;
	}
	struct dwl_ext_workspace *workspace = addon->data;
	dwl_transaction_op_add(addon->ctx, WS_PENDING_WS_DEACTIVATE, workspace,
						   NULL);
}

/*
 * Handle client request to assign workspace to different group
 * Adds to transaction queue for atomic application
 */
static void workspace_handle_assign(struct wl_client *client,
									struct wl_resource *resource,
									struct wl_resource *new_group_resource) {
	struct dwl_wl_resource_addon *addon = wl_resource_get_user_data(resource);
	if (!addon) {
		return;
	}
	struct dwl_ext_workspace *workspace = addon->data;
	struct dwl_wl_resource_addon *grp_addon =
		wl_resource_get_user_data(new_group_resource);
	if (!grp_addon) {
		return;
	}
	struct dwl_ext_workspace_group *new_grp = grp_addon->data;
	dwl_transaction_op_add(addon->ctx, WS_PENDING_WS_ASSIGN, workspace,
						   new_grp);
}

/*
 * Handle client request to remove workspace
 * Adds to transaction queue for atomic application
 */
static void workspace_handle_remove(struct wl_client *client,
									struct wl_resource *resource) {
	struct dwl_wl_resource_addon *addon = wl_resource_get_user_data(resource);
	if (!addon) {
		return;
	}
	struct dwl_ext_workspace *workspace = addon->data;
	dwl_transaction_op_add(addon->ctx, WS_PENDING_WS_REMOVE, workspace, NULL);
}

/* Workspace protocol interface definition */
static const struct ext_workspace_handle_v1_interface workspace_impl = {
	.destroy = workspace_handle_destroy,
	.activate = workspace_handle_activate,
	.deactivate = workspace_handle_deactivate,
	.assign = workspace_handle_assign,
	.remove = workspace_handle_remove,
};

/*
 * Cleanup when workspace resource is destroyed
 * Removes resource from workspace's resource list
 */
static void workspace_instance_resource_destroy(struct wl_resource *resource) {
	struct dwl_wl_resource_addon *addon = wl_resource_get_user_data(resource);
	if (addon) {
		dwl_resource_addon_destroy(addon);
		wl_resource_set_user_data(resource, NULL);
	}

	wl_list_remove(wl_resource_get_link(resource));
}

/*
 * Create new workspace resource for client
 * Links it to the actual workspace and transaction context
 */
static struct wl_resource *
workspace_resource_create(struct dwl_ext_workspace *workspace,
						  struct wl_resource *manager_resource,
						  struct dwl_transaction_session_context *ctx) {
	struct wl_client *client = wl_resource_get_client(manager_resource);
	struct wl_resource *resource =
		wl_resource_create(client, &ext_workspace_handle_v1_interface,
						   wl_resource_get_version(manager_resource), 0);
	if (!resource) {
		wl_client_post_no_memory(client);
		return NULL;
	}

	struct dwl_wl_resource_addon *addon = dwl_resource_addon_create(ctx);
	addon->data = workspace;

	wl_resource_set_implementation(resource, &workspace_impl, addon,
								   workspace_instance_resource_destroy);

	wl_list_insert(&workspace->resources, wl_resource_get_link(resource));
	return resource;
}

/*
 * Send workspace state to client(s)
 * If target is NULL, sends to all bound clients
 */
static void workspace_send_state(struct dwl_ext_workspace *workspace,
								 struct wl_resource *target) {
	if (target) {
		ext_workspace_handle_v1_send_state(target, workspace->state);
	} else {
		struct wl_resource *resource;
		wl_resource_for_each(resource, &workspace->resources) {
			ext_workspace_handle_v1_send_state(resource, workspace->state);
		}
	}
}

/*
 * Send initial workspace state to new client
 * Includes capabilities, coordinates, name and ID
 */
static void workspace_send_initial_state(struct dwl_ext_workspace *workspace,
										 struct wl_resource *resource) {
	ext_workspace_handle_v1_send_capabilities(resource,
											  workspace->capabilities);
	if (workspace->coordinates.size > 0) {
		ext_workspace_handle_v1_send_coordinates(resource,
												 &workspace->coordinates);
	}
	if (workspace->name) {
		ext_workspace_handle_v1_send_name(resource, workspace->name);
	}
	if (workspace->id) {
		ext_workspace_handle_v1_send_id(resource, workspace->id);
	}
}

/*
 * Update workspace state flags
 * Queues done event if state actually changed
 */
static void workspace_set_state(struct dwl_ext_workspace *workspace,
								enum ext_workspace_handle_v1_state state,
								bool enabled) {
	if (!!(workspace->state_pending & state) == enabled) {
		return;
	}

	if (enabled) {
		workspace->state_pending |= state;
	} else {
		workspace->state_pending &= ~state;
	}
	ext_manager_schedule_done_event(workspace->manager);
}

/*
 * Cleanup workspace creation event data when transaction op is destroyed
 */
static void
ws_create_workspace_handle_transaction_op_destroy(struct wl_listener *listener,
												  void *data) {
	struct ws_create_workspace_event *ev =
		wl_container_of(listener, ev, on.transaction_op_destroy);
	wl_list_remove(&ev->on.transaction_op_destroy.link);
	free(ev->name);
	free(ev);
}

/*
 * Handle client request to create new workspace in group
 * Creates event data and adds to transaction queue
 */
static void group_handle_create_workspace(struct wl_client *client,
										  struct wl_resource *resource,
										  const char *name) {
	struct dwl_wl_resource_addon *addon = wl_resource_get_user_data(resource);
	if (!addon) {
		return;
	}

	struct dwl_ext_workspace_group *group = addon->data;
	struct ws_create_workspace_event *ev = ecalloc(1, sizeof(*ev));
	ev->name = strdup(name);

	struct dwl_transaction_op *transaction_op =
		dwl_transaction_op_add(addon->ctx, WS_PENDING_WS_CREATE, group, ev);

	ev->on.transaction_op_destroy.notify =
		ws_create_workspace_handle_transaction_op_destroy;
	wl_signal_add(&transaction_op->events.destroy,
				  &ev->on.transaction_op_destroy);
}

/*
 * Handle client request to destroy group resource
 * Just destroys client's resource, not the actual group
 */
static void group_handle_destroy(struct wl_client *client,
								 struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

/* Workspace group protocol interface definition */
static const struct ext_workspace_group_handle_v1_interface group_impl = {
	.create_workspace = group_handle_create_workspace,
	.destroy = group_handle_destroy,
};

/*
 * Cleanup when group resource is destroyed
 * Removes resource from group's resource list
 */
static void group_instance_resource_destroy(struct wl_resource *resource) {
	struct dwl_wl_resource_addon *addon = wl_resource_get_user_data(resource);
	if (addon) {
		dwl_resource_addon_destroy(addon);
		wl_resource_set_user_data(resource, NULL);
	}
	wl_list_remove(wl_resource_get_link(resource));
}

/*
 * Create new group resource for client
 * Links it to the actual group and transaction context
 */
static struct wl_resource *
group_resource_create(struct dwl_ext_workspace_group *group,
					  struct wl_resource *manager_resource,
					  struct dwl_transaction_session_context *ctx) {
	struct wl_client *client = wl_resource_get_client(manager_resource);
	struct wl_resource *resource =
		wl_resource_create(client, &ext_workspace_group_handle_v1_interface,
						   wl_resource_get_version(manager_resource), 0);
	if (!resource) {
		wl_client_post_no_memory(client);
		return NULL;
	}

	struct dwl_wl_resource_addon *addon = dwl_resource_addon_create(ctx);
	addon->data = group;

	wl_resource_set_implementation(resource, &group_impl, addon,
								   group_instance_resource_destroy);

	wl_list_insert(&group->resources, wl_resource_get_link(resource));
	return resource;
}

/*
 * Send group state to client
 * Includes capabilities and initial output/workspace state
 */
static void group_send_state(struct dwl_ext_workspace_group *group,
							 struct wl_resource *resource) {
	ext_workspace_group_handle_v1_send_capabilities(resource,
													group->capabilities);

	ext_group_output_send_initial_state(group, resource);
}

/*
 * Handle client commit request - applies all pending changes
 * Emits appropriate signals for each pending operation
 */
static void manager_handle_commit(struct wl_client *client,
								  struct wl_resource *resource) {
	struct dwl_wl_resource_addon *addon = wl_resource_get_user_data(resource);
	if (!addon) {
		return;
	}

	struct dwl_ext_workspace *workspace;
	struct dwl_ext_workspace_group *group;
	struct dwl_transaction_op *trans_op, *trans_op_tmp;
	dwl_transaction_for_each_safe(trans_op, trans_op_tmp, addon->ctx) {
		switch (trans_op->change) {
		case WS_PENDING_WS_CREATE:
			group = trans_op->src;
			struct ws_create_workspace_event *ev = trans_op->data;
			wl_signal_emit_mutable(&group->events.create_workspace, ev->name);
			break;
		case WS_PENDING_WS_ACTIVATE:
			workspace = trans_op->src;
			wl_signal_emit_mutable(&workspace->events.activate, NULL);
			break;
		case WS_PENDING_WS_DEACTIVATE:
			workspace = trans_op->src;
			wl_signal_emit_mutable(&workspace->events.deactivate, NULL);
			break;
		case WS_PENDING_WS_REMOVE:
			workspace = trans_op->src;
			wl_signal_emit_mutable(&workspace->events.remove, NULL);
			break;
		case WS_PENDING_WS_ASSIGN:
			workspace = trans_op->src;
			wl_signal_emit_mutable(&workspace->events.assign, trans_op->data);
			break;
		default:
			wlr_log(WLR_ERROR, "Invalid transaction state: %u",
					trans_op->change);
		}

		dwl_transaction_op_destroy(trans_op);
	}
}

/*
 * Handle client stop request - terminates protocol interaction
 */
static void manager_handle_stop(struct wl_client *client,
								struct wl_resource *resource) {
	ext_workspace_manager_v1_send_finished(resource);
	wl_resource_destroy(resource);
}

/* Workspace manager protocol interface definition */
static const struct ext_workspace_manager_v1_interface manager_impl = {
	.commit = manager_handle_commit,
	.stop = manager_handle_stop,
};

/*
 * Cleanup when manager resource is destroyed
 * Removes resource from manager's resource list
 */
static void manager_instance_resource_destroy(struct wl_resource *resource) {
	struct dwl_wl_resource_addon *addon = wl_resource_get_user_data(resource);
	if (addon) {
		dwl_resource_addon_destroy(addon);
		wl_resource_set_user_data(resource, NULL);
	}
	wl_list_remove(wl_resource_get_link(resource));
}

/*
 * Handle new client binding to manager global
 * Sets up all existing groups and workspaces for the client
 */
static void manager_handle_bind(struct wl_client *client, void *data,
								uint32_t version, uint32_t id) {
	struct dwl_ext_workspace_manager *manager = data;
	struct wl_resource *manager_resource = wl_resource_create(
		client, &ext_workspace_manager_v1_interface, version, id);
	if (!manager_resource) {
		wl_client_post_no_memory(client);
		return;
	}

	struct dwl_wl_resource_addon *addon = dwl_resource_addon_create(NULL);
	addon->data = manager;

	wl_resource_set_implementation(manager_resource, &manager_impl, addon,
								   manager_instance_resource_destroy);

	wl_list_insert(&manager->resources, wl_resource_get_link(manager_resource));

	struct dwl_ext_workspace *workspace;
	struct dwl_ext_workspace_group *group;
	wl_list_for_each(group, &manager->groups, link) {
		struct wl_resource *group_resource =
			group_resource_create(group, manager_resource, addon->ctx);
		ext_workspace_manager_v1_send_workspace_group(manager_resource,
													  group_resource);
		group_send_state(group, group_resource);
		wl_list_for_each(workspace, &manager->workspaces, link) {
			if (workspace->group != group) {
				continue;
			}
			struct wl_resource *workspace_resource = workspace_resource_create(
				workspace, manager_resource, addon->ctx);
			ext_workspace_manager_v1_send_workspace(manager_resource,
													workspace_resource);
			workspace_send_initial_state(workspace, workspace_resource);
			workspace_send_state(workspace, workspace_resource);
			ext_workspace_group_handle_v1_send_workspace_enter(
				group_resource, workspace_resource);
		}
	}

	wl_list_for_each(workspace, &manager->workspaces, link) {
		if (workspace->group) {
			continue;
		}
		struct wl_resource *workspace_resource =
			workspace_resource_create(workspace, manager_resource, addon->ctx);
		ext_workspace_manager_v1_send_workspace(manager_resource,
												workspace_resource);
		workspace_send_initial_state(workspace, workspace_resource);
		workspace_send_state(workspace, workspace_resource);
	}
	ext_workspace_manager_v1_send_done(manager_resource);
}

/*
 * Cleanup when display is destroyed
 * Destroys all groups and workspaces
 */
static void manager_handle_display_destroy(struct wl_listener *listener,
										   void *data) {
	struct dwl_ext_workspace_manager *manager =
		wl_container_of(listener, manager, on.display_destroy);

	struct dwl_ext_workspace_group *group, *tmp;
	wl_list_for_each_safe(group, tmp, &manager->groups, link) {
		dwl_ext_workspace_group_destroy(group);
	}

	struct dwl_ext_workspace *ws, *ws_tmp;
	wl_list_for_each_safe(ws, ws_tmp, &manager->workspaces, link) {
		dwl_ext_workspace_destroy(ws);
	}

	if (manager->idle_source) {
		wl_event_source_remove(manager->idle_source);
	}

	wl_list_remove(&manager->on.display_destroy.link);
	wl_global_destroy(manager->global);
	free(manager);
}

/*
 * Idle callback that sends done events to all clients
 * Applies pending state changes and notifies clients
 */
static void manager_idle_send_done(void *data) {
	struct dwl_ext_workspace_manager *manager = data;

	struct dwl_ext_workspace *workspace;
	wl_list_for_each(workspace, &manager->workspaces, link) {
		if (workspace->state != workspace->state_pending) {
			workspace->state = workspace->state_pending;
			workspace_send_state(workspace, /*target*/ NULL);
		}
	}

	struct wl_resource *resource;
	wl_resource_for_each(resource, &manager->resources) {
		ext_workspace_manager_v1_send_done(resource);
	}
	manager->idle_source = NULL;
}

/*
 * Schedule done event to be sent to clients
 * Uses idle handler to batch multiple changes
 */
void ext_manager_schedule_done_event(
	struct dwl_ext_workspace_manager *manager) {
	if (manager->idle_source) {
		return;
	}
	if (!manager->event_loop) {
		return;
	}
	manager->idle_source = wl_event_loop_add_idle(
		manager->event_loop, manager_idle_send_done, manager);
}

/*
 * Send workspace event to all clients of a group
 * Helper function for workspace enter/leave events
 */
static void send_group_workspace_event(struct dwl_ext_workspace_group *group,
									   struct dwl_ext_workspace *workspace,
									   void (*fn)(struct wl_resource *grp_res,
												  struct wl_resource *ws_res)) {
	struct dwl_wl_resource_addon *workspace_addon, *group_addon;
	struct wl_resource *workspace_resource, *group_resource;
	wl_resource_for_each(workspace_resource, &workspace->resources) {
		workspace_addon = wl_resource_get_user_data(workspace_resource);
		wl_resource_for_each(group_resource, &group->resources) {
			group_addon = wl_resource_get_user_data(group_resource);
			if (group_addon->ctx != workspace_addon->ctx) {
				continue;
			}
			fn(group_resource, workspace_resource);
			break;
		}
	}
}

/*
 * Create new workspace manager global
 * Sets up capabilities and initial state
 */
struct dwl_ext_workspace_manager *
dwl_ext_workspace_manager_create(struct wl_display *display, uint32_t caps,
								 uint32_t version) {
	assert(display);
	assert(version <= EXT_WORKSPACE_V1_VERSION);

	struct dwl_ext_workspace_manager *manager = ecalloc(1, sizeof(*manager));
	manager->global =
		wl_global_create(display, &ext_workspace_manager_v1_interface, version,
						 manager, manager_handle_bind);

	if (!manager->global) {
		free(manager);
		return NULL;
	}

	manager->caps = caps;
	manager->event_loop = wl_display_get_event_loop(display);

	manager->on.display_destroy.notify = manager_handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->on.display_destroy);

	wl_list_init(&manager->groups);
	wl_list_init(&manager->workspaces);
	wl_list_init(&manager->resources);
	return manager;
}

/*
 * Create new workspace group
 * Adds it to manager and notifies all clients
 */
struct dwl_ext_workspace_group *
dwl_ext_workspace_group_create(struct dwl_ext_workspace_manager *manager) {
	assert(manager);

	struct dwl_ext_workspace_group *group = ecalloc(1, sizeof(*group));
	group->manager = manager;
	group->capabilities = manager->caps & WS_CAP_GRP_ALL;

	wl_list_init(&group->outputs);
	wl_list_init(&group->resources);
	wl_signal_init(&group->events.create_workspace);
	wl_signal_init(&group->events.destroy);

	wl_list_append(&manager->groups, &group->link);

	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp, &manager->resources) {
		struct dwl_wl_resource_addon *addon =
			wl_resource_get_user_data(resource);
		assert(addon && addon->ctx);
		struct wl_resource *group_resource =
			group_resource_create(group, resource, addon->ctx);
		ext_workspace_manager_v1_send_workspace_group(resource, group_resource);
		group_send_state(group, group_resource);
	}
	ext_manager_schedule_done_event(manager);

	return group;
}

/*
 * Destroy workspace group
 * Cleans up all resources and notifies clients
 */
void dwl_ext_workspace_group_destroy(struct dwl_ext_workspace_group *group) {
	assert(group);
	wl_signal_emit_mutable(&group->events.destroy, NULL);

	struct dwl_ext_workspace *workspace;
	wl_list_for_each(workspace, &group->manager->workspaces, link) {
		if (workspace->group == group) {
			send_group_workspace_event(
				group, workspace,
				ext_workspace_group_handle_v1_send_workspace_leave);
			workspace->group = NULL;
		}
	}

	struct wl_resource *resource, *res_tmp;
	wl_resource_for_each_safe(resource, res_tmp, &group->resources) {
		ext_workspace_group_handle_v1_send_removed(resource);
		struct dwl_wl_resource_addon *addon =
			wl_resource_get_user_data(resource);
		dwl_resource_addon_destroy(addon);
		wl_resource_set_user_data(resource, NULL);
		wl_list_remove(wl_resource_get_link(resource));
		wl_list_init(wl_resource_get_link(resource));
	}

	struct dwl_transaction_op *trans_op, *trans_op_tmp;
	wl_resource_for_each(resource, &group->manager->resources) {
		struct dwl_wl_resource_addon *addon =
			wl_resource_get_user_data(resource);
		dwl_transaction_for_each_safe(trans_op, trans_op_tmp, addon->ctx) {
			if (trans_op->src == group || trans_op->data == group) {
				dwl_transaction_op_destroy(trans_op);
			}
		}
	}

	ext_manager_schedule_done_event(group->manager);

	wl_list_remove(&group->link);
	free(group);
}

/*
 * Create new workspace
 * Adds it to manager and notifies all clients
 */
struct dwl_ext_workspace *
dwl_ext_workspace_create(struct dwl_ext_workspace_manager *manager,
						 const char *id) {
	assert(manager);

	struct dwl_ext_workspace *workspace = ecalloc(1, sizeof(*workspace));

	workspace->state = WS_STATE_INVALID;
	workspace->capabilities = (manager->caps & WS_CAP_WS_ALL) >> 16;
	workspace->manager = manager;
	if (id) {
		workspace->id = strdup(id);
	}

	wl_list_init(&workspace->resources);
	wl_array_init(&workspace->coordinates);
	wl_signal_init(&workspace->events.activate);
	wl_signal_init(&workspace->events.deactivate);
	wl_signal_init(&workspace->events.remove);
	wl_signal_init(&workspace->events.assign);
	wl_signal_init(&workspace->events.destroy);

	wl_list_append(&manager->workspaces, &workspace->link);

	struct dwl_wl_resource_addon *manager_addon;
	struct wl_resource *manager_resource, *workspace_resource;
	wl_resource_for_each(manager_resource, &manager->resources) {
		manager_addon = wl_resource_get_user_data(manager_resource);
		workspace_resource = workspace_resource_create(
			workspace, manager_resource, manager_addon->ctx);
		ext_workspace_manager_v1_send_workspace(manager_resource,
												workspace_resource);
		workspace_send_initial_state(workspace, workspace_resource);
	}

	ext_manager_schedule_done_event(manager);

	return workspace;
}

/*
 * Assign workspace to group
 * Notifies clients about the change
 */
void dwl_ext_workspace_assign_to_group(struct dwl_ext_workspace *workspace,
									   struct dwl_ext_workspace_group *group) {
	assert(workspace);

	if (workspace->group == group) {
		return;
	}

	if (workspace->group) {
		send_group_workspace_event(
			workspace->group, workspace,
			ext_workspace_group_handle_v1_send_workspace_leave);
		ext_manager_schedule_done_event(workspace->manager);
	}
	workspace->group = group;

	if (!group) {
		return;
	}

	send_group_workspace_event(
		group, workspace, ext_workspace_group_handle_v1_send_workspace_enter);
	ext_manager_schedule_done_event(workspace->manager);
}

/*
 * Set workspace name
 * Updates all clients if name changed
 */
void dwl_ext_workspace_set_name(struct dwl_ext_workspace *workspace,
								const char *name) {
	assert(workspace);
	assert(name);

	if (!workspace->name || strcmp(workspace->name, name)) {
		free(workspace->name);
		workspace->name = strdup(name);
		struct wl_resource *resource;
		wl_resource_for_each(resource, &workspace->resources) {
			ext_workspace_handle_v1_send_name(resource, workspace->name);
		}
	}
	ext_manager_schedule_done_event(workspace->manager);
}

/*
 * Set workspace active state
 * Queues state change for atomic update
 */
void dwl_ext_workspace_set_active(struct dwl_ext_workspace *workspace,
								  bool enabled) {
	assert(workspace);
	workspace_set_state(workspace, EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE,
						enabled);
}

/*
 * Set workspace urgent state
 * Queues state change for atomic update
 */
void dwl_ext_workspace_set_urgent(struct dwl_ext_workspace *workspace,
								  bool enabled) {
	assert(workspace);
	workspace_set_state(workspace, EXT_WORKSPACE_HANDLE_V1_STATE_URGENT,
						enabled);
}

/*
 * Set workspace hidden state
 * Queues state change for atomic update
 */
void dwl_ext_workspace_set_hidden(struct dwl_ext_workspace *workspace,
								  bool enabled) {
	assert(workspace);
	workspace_set_state(workspace, EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN,
						enabled);
}

/*
 * Set workspace coordinates
 * Updates all clients with new coordinates
 */
void dwl_ext_workspace_set_coordinates(struct dwl_ext_workspace *workspace,
									   struct wl_array *coordinates) {
	assert(workspace);
	assert(coordinates);

	wl_array_release(&workspace->coordinates);
	wl_array_init(&workspace->coordinates);
	wl_array_copy(&workspace->coordinates, coordinates);

	struct wl_resource *resource;
	wl_resource_for_each(resource, &workspace->resources) {
		ext_workspace_handle_v1_send_coordinates(resource,
												 &workspace->coordinates);
	}
	ext_manager_schedule_done_event(workspace->manager);
}

/*
 * Destroy workspace
 * Cleans up all resources and notifies clients
 */
void dwl_ext_workspace_destroy(struct dwl_ext_workspace *workspace) {
	assert(workspace);

	wl_signal_emit_mutable(&workspace->events.destroy, NULL);

	if (workspace->group) {
		send_group_workspace_event(
			workspace->group, workspace,
			ext_workspace_group_handle_v1_send_workspace_leave);
	}

	struct wl_resource *ws_res, *ws_tmp;
	wl_resource_for_each_safe(ws_res, ws_tmp, &workspace->resources) {
		ext_workspace_handle_v1_send_removed(ws_res);
		struct dwl_wl_resource_addon *ws_addon =
			wl_resource_get_user_data(ws_res);
		dwl_resource_addon_destroy(ws_addon);
		wl_resource_set_user_data(ws_res, NULL);
		wl_list_remove(wl_resource_get_link(ws_res));
		wl_list_init(wl_resource_get_link(ws_res));
	}
	ext_manager_schedule_done_event(workspace->manager);

	struct wl_resource *resource;
	struct dwl_transaction_op *trans_op, *trans_op_tmp;
	wl_resource_for_each(resource, &workspace->manager->resources) {
		struct dwl_wl_resource_addon *addon =
			wl_resource_get_user_data(resource);
		dwl_transaction_for_each_safe(trans_op, trans_op_tmp, addon->ctx) {
			if (trans_op->src == workspace) {
				dwl_transaction_op_destroy(trans_op);
			}
		}
	}

	wl_list_remove(&workspace->link);
	wl_array_release(&workspace->coordinates);
	free(workspace->id);
	free(workspace->name);
	workspace->id = NULL;
	workspace->name = NULL;
	free(workspace);
}

/*
 * Helper to send output enter/leave events to matching clients
 */
static void group_output_send_event(
	struct wl_list *group_resources, struct wl_list *output_resources,
	void (*notifier)(struct wl_resource *group, struct wl_resource *output)) {
	struct wl_client *client;
	struct wl_resource *group_resource, *output_resource;
	wl_resource_for_each(group_resource, group_resources) {
		client = wl_resource_get_client(group_resource);
		wl_resource_for_each(output_resource, output_resources) {
			if (wl_resource_get_client(output_resource) == client) {
				notifier(group_resource, output_resource);
			}
		}
	}
}

/*
 * Cleanup group output tracking
 * Notifies clients and removes all listeners
 */
static void group_output_destroy(struct group_output *group_output) {
	group_output_send_event(&group_output->group->resources,
							&group_output->wlr_output->resources,
							ext_workspace_group_handle_v1_send_output_leave);

	ext_manager_schedule_done_event(group_output->group->manager);

	wl_list_remove(&group_output->link);
	wl_list_remove(&group_output->on.group_destroy.link);
	wl_list_remove(&group_output->on.output_bind.link);
	wl_list_remove(&group_output->on.output_destroy.link);
	free(group_output);
}

/*
 * Handle new client binding to output
 * Notifies client about output membership in group
 */
static void handle_output_bind(struct wl_listener *listener, void *data) {
	struct group_output *group_output =
		wl_container_of(listener, group_output, on.output_bind);

	struct wlr_output_event_bind *event = data;
	struct wl_client *client = wl_resource_get_client(event->resource);

	bool sent = false;
	struct wl_resource *group_resource;
	wl_resource_for_each(group_resource, &group_output->group->resources) {
		if (wl_resource_get_client(group_resource) == client) {
			ext_workspace_group_handle_v1_send_output_enter(group_resource,
															event->resource);
			sent = true;
		}
	}
	if (!sent) {
		return;
	}

	struct wl_resource *manager_resource;
	struct wl_list *manager_resources =
		&group_output->group->manager->resources;
	wl_resource_for_each(manager_resource, manager_resources) {
		if (wl_resource_get_client(manager_resource) == client) {
			ext_workspace_manager_v1_send_done(manager_resource);
		}
	}
}

/*
 * Handle output destruction
 * Cleans up group output tracking
 */
static void handle_output_destroy(struct wl_listener *listener, void *data) {
	struct group_output *group_output =
		wl_container_of(listener, group_output, on.output_destroy);
	group_output_destroy(group_output);
}

/*
 * Handle group destruction
 * Cleans up group output tracking
 */
static void handle_group_destroy(struct wl_listener *listener, void *data) {
	struct group_output *group_output =
		wl_container_of(listener, group_output, on.group_destroy);
	group_output_destroy(group_output);
}

/*
 * Send initial output state to new client
 * Notifies about all outputs in the group
 */
void ext_group_output_send_initial_state(struct dwl_ext_workspace_group *group,
										 struct wl_resource *group_resource) {
	struct group_output *group_output;
	struct wl_resource *output_resource;
	struct wl_client *client = wl_resource_get_client(group_resource);
	wl_list_for_each(group_output, &group->outputs, link) {
		wl_resource_for_each(output_resource,
							 &group_output->wlr_output->resources) {
			if (wl_resource_get_client(output_resource) == client) {
				ext_workspace_group_handle_v1_send_output_enter(
					group_resource, output_resource);
			}
		}
	}
}

/*
 * Add output to workspace group
 * Sets up tracking and notifies clients
 */
void dwl_ext_workspace_group_output_enter(struct dwl_ext_workspace_group *group,
										  struct wlr_output *wlr_output) {
	struct group_output *group_output;
	wl_list_for_each(group_output, &group->outputs, link) {
		if (group_output->wlr_output == wlr_output) {
			return;
		}
	}
	group_output = ecalloc(1, sizeof(*group_output));
	group_output->wlr_output = wlr_output;
	group_output->group = group;

	group_output->on.group_destroy.notify = handle_group_destroy;
	wl_signal_add(&group->events.destroy, &group_output->on.group_destroy);

	group_output->on.output_bind.notify = handle_output_bind;
	wl_signal_add(&wlr_output->events.bind, &group_output->on.output_bind);

	group_output->on.output_destroy.notify = handle_output_destroy;
	wl_signal_add(&wlr_output->events.destroy,
				  &group_output->on.output_destroy);

	wl_list_insert(&group->outputs, &group_output->link);

	group_output_send_event(&group_output->group->resources,
							&group_output->wlr_output->resources,
							ext_workspace_group_handle_v1_send_output_enter);

	ext_manager_schedule_done_event(group->manager);
}

/*
 * Remove output from workspace group
 * Cleans up tracking and notifies clients
 */
void dwl_ext_workspace_group_output_leave(struct dwl_ext_workspace_group *group,
										  struct wlr_output *wlr_output) {
	struct group_output *tmp;
	struct group_output *group_output = NULL;
	wl_list_for_each(tmp, &group->outputs, link) {
		if (tmp->wlr_output == wlr_output) {
			group_output = tmp;
			break;
		}
	}
	if (!group_output) {
		wlr_log(WLR_ERROR, "output %s was never entered", wlr_output->name);
		return;
	}

	group_output_destroy(group_output);
}

/*
 * Switch to target workspace
 * Handles both regular workspaces and overview
 */
void goto_workspace(struct workspace *target) {
	unsigned int tag;
	tag = 1 << (target->tag - 1);
	if (target->tag == 0) {
		toggleoverview(&(Arg){.i = -1});
		return;
	} else {
		view(&(Arg){.ui = tag}, true);
	}
}

/*
 * Handle workspace activation from protocol
 * Switches to the requested workspace
 */
static void handle_ext_workspace_activate(struct wl_listener *listener,
										  void *data) {
	struct workspace *workspace =
		wl_container_of(listener, workspace, activate);
	goto_workspace(workspace);
	wlr_log(WLR_INFO, "ext activating workspace %d", workspace->tag);
}

/*
 * Get workspace name from tag
 * Returns static string for predefined tags
 */
static const char *get_name_from_tag(unsigned int tag) {
	static const char *names[] = {"overview", "1", "2", "3", "4",
								  "5",		  "6", "7", "8", "9"};
	return (tag < sizeof(names) / sizeof(names[0])) ? names[tag] : NULL;
}

/*
 * Destroy workspace and clean up resources
 */
void destroy_workspace(struct workspace *workspace) {
	wl_list_remove(&workspace->activate.link);
	dwl_ext_workspace_destroy(workspace->ext_workspace);
	wl_list_remove(&workspace->link);
	free(workspace);
}

/*
 * Clean up all workspaces associated with monitor
 */
void cleanup_workspaces_by_monitor(Monitor *m) {
	struct workspace *workspace, *tmp;
	wl_list_for_each_safe(workspace, tmp, &workspaces, link) {
		if (workspace->m == m) {
			destroy_workspace(workspace);
		}
	}
}

/*
 * Remove workspace by tag and monitor
 */
static void remove_workspace_by_tag(unsigned int tag, Monitor *m) {
	struct workspace *workspace, *tmp;
	wl_list_for_each_safe(workspace, tmp, &workspaces, link) {
		if (workspace->tag == tag && workspace->m == m) {
			destroy_workspace(workspace);
			return;
		}
	}
}

/*
 * Add new workspace by tag and monitor
 * Creates both internal and protocol objects
 */
static void add_workspace_by_tag(int tag, Monitor *m) {
	const char *name = get_name_from_tag(tag);

	struct workspace *workspace = ecalloc(1, sizeof(*workspace));
	wl_list_append(&workspaces, &workspace->link);

	workspace->tag = tag;
	workspace->m = m;
	workspace->ext_workspace =
		dwl_ext_workspace_create(ext_manager, /*id*/ NULL);
	dwl_ext_workspace_assign_to_group(workspace->ext_workspace, m->ext_group);
	dwl_ext_workspace_set_name(workspace->ext_workspace, name);
	workspace->activate.notify = handle_ext_workspace_activate;
	wl_signal_add(&workspace->ext_workspace->events.activate,
				  &workspace->activate);
}

/*
 * Initialize workspace system
 * Creates global manager and initial state
 */
void workspaces_init() {
	/* Create the global workspace manager with activation capability */
	ext_manager = dwl_ext_workspace_manager_create(dpy, WS_CAP_WS_ACTIVATE, 1);
	/* Initialize the global workspaces list */
	wl_list_init(&workspaces);
}