#include <wayland-server-core.h>

struct dwl_transaction_op {
	uint32_t change;
	void *src;
	void *data;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_list link;
};

struct dwl_transaction_session_context {
	int ref_count;
	struct wl_list transaction_ops;
};

struct dwl_wl_resource_addon {
	struct dwl_transaction_session_context *ctx;
	void *data;
};

struct dwl_wl_resource_addon *
dwl_resource_addon_create(struct dwl_transaction_session_context *ctx);

struct dwl_transaction_op *
dwl_transaction_op_add(struct dwl_transaction_session_context *ctx,
					   uint32_t pending_change, void *src, void *data);

void dwl_transaction_op_destroy(struct dwl_transaction_op *transaction_op);

void dwl_resource_addon_destroy(struct dwl_wl_resource_addon *addon);

#define dwl_transaction_for_each(transaction_op, ctx)                          \
	wl_list_for_each(transaction_op, &(ctx)->transaction_ops, link)

#define dwl_transaction_for_each_safe(trans_op, trans_op_tmp, ctx)             \
	wl_list_for_each_safe(trans_op, trans_op_tmp, &(ctx)->transaction_ops, link)
