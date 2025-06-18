#include "ext-workspace.h"
#include "../common/list.h"

/* Double use: as config in config/rcxml.c and as instance in workspaces.c */
struct workspace {
	struct wl_list link;

	char *name;
	unsigned int tag;
	struct wlr_scene_tree *tree;

	struct dwl_ext_workspace *ext_workspace;
	struct {
		struct wl_listener activate;
		struct wl_listener deactivate;
		struct wl_listener assign;
		struct wl_listener remove;
	} on_ext;
};

/* Workspaces */
struct {
	struct wl_list all;  /* struct workspace.link */
	struct workspace *current;
	struct workspace *last;
	struct dwl_cosmic_workspace_manager *cosmic_manager;
	struct dwl_cosmic_workspace_group *cosmic_group;
	struct dwl_ext_workspace_manager *ext_manager;
	struct dwl_ext_workspace_group *ext_group;
	struct {
		struct wl_listener layout_output_added;
	} on;
} workspaces;

/* ext workspace handlers */
static void
handle_ext_workspace_activate(struct wl_listener *listener, void *data)
{
	struct workspace *workspace = wl_container_of(listener, workspace, on_ext.activate);
	unsigned int target;
	target = 1<<(workspace->tag-1);
	if(workspace->tag == 0) {
		toggleoverview(&(Arg){.i = -1});
		return;
	} else {
		view(&(Arg){.ui = target}, true);
	}
	wlr_log(WLR_INFO, "ext activating workspace %s", workspace->name);
}


/* Internal API */
static void
add_workspace(int tag)
{
	char *name = NULL;
	switch (tag) {
	case 0:
		name = "overview";
		break;
	case 1:
		name = "1";
		break;
	case 2:
		name = "2";
		break;
	case 3:
		name = "3";
		break;
	case 4:
		name = "4";
		break;
	case 5:
		name = "5";
		break;
	case 6:
		name = "6";
		break;
	case 7:
		name = "7";
		break;
	case 8:
		name = "8";
		break;
	case 9:
		name = "9";
		break;
	}
	struct workspace *workspace = znew(*workspace);
	workspace->name = xstrdup(name);
	workspace->tag = tag;
	workspace->tree = wlr_scene_tree_create(&scene->tree);
	wl_list_append(&workspaces.all, &workspace->link);
	if (!workspaces.current) {
		workspaces.current = workspace;
	} else {
		wlr_scene_node_set_enabled(&workspace->tree->node, false);
	}

	bool active = workspaces.current == workspace;

	/* ext */
	workspace->ext_workspace = dwl_ext_workspace_create(
		workspaces.ext_manager, /*id*/ NULL);
	dwl_ext_workspace_assign_to_group(workspace->ext_workspace, workspaces.ext_group);
	dwl_ext_workspace_set_name(workspace->ext_workspace, name);
	dwl_ext_workspace_set_active(workspace->ext_workspace, active);

	workspace->on_ext.activate.notify = handle_ext_workspace_activate;
	wl_signal_add(&workspace->ext_workspace->events.activate,
		&workspace->on_ext.activate);
}


void
workspaces_init()
{
	int i;

	workspaces.ext_manager = dwl_ext_workspace_manager_create(dpy,WS_CAP_WS_ACTIVATE, 1);


	workspaces.ext_group = dwl_ext_workspace_group_create(workspaces.ext_manager);

	wl_list_init(&workspaces.all);


	for (i = 0; i <= LENGTH(tags); i++) {
		add_workspace(i);
	}
}
