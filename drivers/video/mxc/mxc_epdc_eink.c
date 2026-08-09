/*
 * Independent implementation of the Lab126/Freescale REAGL helper.
 *
 * The framebuffer driver calls this file through linux/mxcfb_eink.h.  The
 * original object is a processing library rather than a hardware driver: it
 * operates on RGB565 working buffers and keeps three scan lines of state
 * between updates.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <asm/cacheflush.h>

#include <linux/mxcfb_eink.h>

#define REAGL_GREEN_MASK		0x03e0
#define REAGL_BLUE_MASK			0x001f
#define REAGL_GREEN_LIMIT		0x039f
#define REAGL_BLUE_LIMIT			28
#define REAGL_GRAY_GREEN			0x03a0

#define REAGL_LINE_COUNT		3
#define REAGL_LINE_PADDING		2
#define REAGL_MAX_WIDTH			32768

/* The original allocates the 2-KiB kmalloc cache for this table. */
#define REAGL_ACTIVE_MASK_ENTRIES	64

/*
 * These are deliberately global with the original names.  They are not part
 * of the public ABI, but retaining the layout makes the state visible to the
 * same diagnostics and keeps the implementation's lifetime semantics clear.
 */
u32 *nextWhite;
u32 *currWhite;
u16 *workingLines;
u32 *activeMaskMatrix;
static u32 reaGLD_update_counter;

/* 64-entry active-mask table recovered from the original .data section. */
static u32 activeMask[REAGL_ACTIVE_MASK_ENTRIES] = {
	0, 1, 5, 6, 7, 2, 3, 4,
	2, 3, 4, 0, 1, 5, 6, 7,
	5, 6, 7, 2, 3, 4, 0, 1,
	4, 0, 1, 5, 6, 7, 2, 3,
	7, 2, 3, 4, 0, 1, 5, 6,
	1, 5, 6, 7, 2, 3, 4, 0,
	3, 4, 0, 1, 5, 6, 7, 2,
	6, 7, 2, 3, 4, 0, 1, 5,
};

struct reagl_region {
	unsigned long left;
	unsigned long top;
	unsigned long width;
	unsigned long height;
};

static inline bool reagl_green_light(wb_t pixel)
{
	return (pixel & REAGL_GREEN_MASK) > REAGL_GREEN_LIMIT;
}

static inline bool reagl_blue_light(wb_t pixel)
{
	return (pixel & REAGL_BLUE_MASK) > REAGL_BLUE_LIMIT;
}

static inline bool reagl_is_light(wb_t pixel)
{
	return reagl_green_light(pixel) && reagl_blue_light(pixel);
}

static inline bool reagl_v4_gray_state(wb_t pixel)
{
	/* The v4 guard is reached only after the target is known to be light. */
	return (((pixel & REAGL_GREEN_MASK) >> 5) & ~2U) == 29;
}

static inline wb_t reagl_gray(wb_t pixel)
{
	return (pixel & ~REAGL_GREEN_MASK) | REAGL_GRAY_GREEN;
}

static inline wb_t reagl_white(wb_t pixel)
{
	return pixel | REAGL_GREEN_MASK;
}

static int reagl_make_region(struct reagl_region *region,
				     struct mxcfb_rect *update_region,
				     long frame_width, long frame_height)
{
	unsigned long left;
	unsigned long top;
	unsigned long width;
	unsigned long height;

	if (!region || !update_region || frame_width <= 0 || frame_height <= 0 ||
		frame_width > REAGL_MAX_WIDTH || frame_height > REAGL_MAX_WIDTH)
		return 0;

	left = update_region->left;
	top = update_region->top;
	width = update_region->width;
	height = update_region->height;
	if (!width || !height || left >= (unsigned long)frame_width ||
		top >= (unsigned long)frame_height)
		return 0;

	/* Valid EPDC updates never need clipping; this also protects the ABI. */
	if (width > (unsigned long)frame_width - left)
		width = (unsigned long)frame_width - left;
	if (height > (unsigned long)frame_height - top)
		height = (unsigned long)frame_height - top;
	if (!width || !height)
		return 0;

	region->left = left;
	region->top = top;
	region->width = width;
	region->height = height;
	return 1;
}

static void reagl_flush_caches(void)
{
	/* Match the original entry sequence: whole CPU cache, then outer cache. */
	__cpuc_flush_kern_all();
	outer_flush_all();
}

static unsigned int reagl_dark_cardinal(const wb_t *buffer,
					 unsigned long x, unsigned long y,
					 unsigned long frame_width,
					 unsigned long frame_height)
{
	unsigned int dark = 0;
	unsigned long index = y * frame_width + x;

	if (x && !reagl_is_light(buffer[index - 1]))
		dark++;
	if (x + 1 < frame_width && !reagl_is_light(buffer[index + 1]))
		dark++;
	if (y && !reagl_is_light(buffer[index - frame_width]))
		dark++;
	if (y + 1 < frame_height &&
		!reagl_is_light(buffer[index + frame_width]))
		dark++;
	return dark;
}

/* Exact logical form of do_reagl_processing_v_2_lab126. */
static void reagl_process_v2(wb_t *buffer, const struct reagl_region *region,
				     long frame_width, long frame_height)
{
	unsigned long x;
	unsigned long y;
	unsigned long end_x;
	unsigned long end_y;
	unsigned long index;

	if (!buffer || !region || region->width < 3 || region->height < 3)
		return;

	end_x = region->left + region->width - 1;
	end_y = region->top + region->height - 1;
	for (y = region->top + 1; y < end_y; y++) {
		for (x = region->left + 1; x < end_x; x++) {
			index = y * (unsigned long)frame_width + x;
			if (!reagl_is_light(buffer[index]))
				continue;
			if (reagl_dark_cardinal(buffer, x, y,
					(unsigned long)frame_width,
					(unsigned long)frame_height) > 2)
				buffer[index] = reagl_gray(buffer[index]);
		}
	}
}

static void reagl_v4_apply(wb_t *target, const wb_t *neighbours,
				   unsigned int neighbour_count, bool allow_white)
{
	unsigned int dark = 0;
	unsigned int i;
	bool all_green = true;
	wb_t pixel;

	if (!target)
		return;
	pixel = *target;
	if (!reagl_is_light(pixel) || reagl_v4_gray_state(pixel))
		return;

	for (i = 0; i < neighbour_count; i++) {
		if (!reagl_is_light(neighbours[i]))
			dark++;
		if (!reagl_green_light(neighbours[i]))
			all_green = false;
	}

	if (dark >= 3) {
		*target = reagl_gray(pixel);
	} else if (allow_white && dark && all_green) {
		*target = reagl_white(pixel);
	}
}

/*
 * The v4 object is an unrolled 2-D stencil.  Each iteration is gated by the
 * pixel immediately to the right of the central target.  It then visits the
 * two horizontal neighbours and the two pixels one row above/below that
 * gate.  The top and bottom passes reverse the vertical pair and use a
 * three-neighbour rule for the boundary pixel.
 */
static void reagl_v4_group(wb_t *buffer, unsigned long stride,
				   unsigned long row, unsigned long column,
				   int c_row, int d_row)
{
	const wb_t *gate;
	wb_t neighbours[4];
	wb_t *target;
	unsigned long c_outer_row;

	gate = buffer + row * stride + column;
	if (reagl_is_light(*gate))
		return;

	target = buffer + row * stride + column - 1;
	neighbours[0] = target[-1];
	neighbours[1] = *gate;
	neighbours[2] = *(buffer + c_row * stride + column - 1);
	neighbours[3] = *(buffer + d_row * stride + column - 1);
	reagl_v4_apply(target, neighbours, 4, true);

	target = buffer + row * stride + column + 1;
	neighbours[0] = target[1];
	neighbours[1] = *gate;
	neighbours[2] = *(buffer + c_row * stride + column + 1);
	neighbours[3] = *(buffer + d_row * stride + column + 1);
	reagl_v4_apply(target, neighbours, 4, true);

	target = buffer + c_row * stride + column;
	neighbours[0] = *(buffer + row * stride + column);
	neighbours[1] = target[-1];
	neighbours[2] = target[1];
	c_outer_row = c_row > (int)row ? (unsigned long)c_row + 1 :
		(unsigned long)c_row - 1;
	neighbours[3] = *(buffer + c_outer_row * stride + column);
	reagl_v4_apply(target, neighbours, 4, true);

	target = buffer + d_row * stride + column;
	neighbours[0] = *(buffer + row * stride + column);
	neighbours[1] = target[-1];
	neighbours[2] = target[1];
	reagl_v4_apply(target, neighbours, 3, false);
}

static void reagl_process_v4(wb_t *buffer, const struct reagl_region *region,
				     long frame_width, long frame_height)
{
	unsigned long column;
	unsigned long row;
	unsigned long stride;

	if (!buffer || !region || frame_width <= 0 || frame_height <= 0 ||
		region->width <= 4 || region->height < 3)
		return;

	stride = (unsigned long)frame_width;
	/* Top pass: center row 1; the three-neighbour target is row 0. */
	for (column = 2; column < region->width - 2; column++)
		reagl_v4_group(buffer + region->top * stride + region->left,
				       stride, 1, column, 2, 0);

	/* Interior pass: center rows 2 through height-3. */
	for (row = 2; row < region->height - 2; row++)
		for (column = 2; column < region->width - 2; column++)
			reagl_v4_group(buffer + region->top * stride + region->left,
				       stride, row, column, row - 1, row + 1);

	/* Bottom pass: center row height-2; the boundary target is the last row. */
	for (column = 2; column < region->width - 2; column++)
		reagl_v4_group(buffer + region->top * stride + region->left,
				       stride, region->height - 2, column,
				       region->height - 3, region->height - 1);
}

struct reagl_state_ring {
	u32 *green[REAGL_LINE_COUNT];
	u32 *light[REAGL_LINE_COUNT];
	wb_t *old[REAGL_LINE_COUNT];
	unsigned long frame_width;
};

static void reagl_ring_bind(struct reagl_state_ring *ring,
				    long frame_width)
{
	unsigned long state_stride = (unsigned long)frame_width +
		REAGL_LINE_PADDING;
	unsigned int line;

	ring->frame_width = (unsigned long)frame_width;
	for (line = 0; line < REAGL_LINE_COUNT; line++) {
		ring->green[line] = nextWhite + line * state_stride + 1;
		ring->light[line] = currWhite + line * state_stride + 1;
		ring->old[line] = workingLines + line * frame_width;
	}
}

static void reagl_ring_clear_line(struct reagl_state_ring *ring,
					 unsigned int line)
{
	unsigned long bytes = (ring->frame_width + REAGL_LINE_PADDING) *
		sizeof(u32);

	memset(ring->green[line] - 1, 0, bytes);
	memset(ring->light[line] - 1, 0, bytes);
}

static void reagl_ring_classify(struct reagl_state_ring *ring,
					unsigned int line, const wb_t *buffer,
					unsigned long frame_row, unsigned long left,
					unsigned long width)
{
	unsigned long x;

	for (x = 0; x < width; x++) {
		wb_t pixel = buffer[frame_row * ring->frame_width + left + x];
		bool green = reagl_green_light(pixel);

		ring->green[line][left + x] = green;
		ring->light[line][left + x] = green &&
			reagl_blue_light(pixel);
	}
}

static void reagl_ring_copy_old(struct reagl_state_ring *ring,
					unsigned int line, const wb_t *buffer,
					unsigned long frame_row)
{
	memcpy(ring->old[line], buffer + frame_row * ring->frame_width,
			ring->frame_width * sizeof(wb_t));
}

static void reagl_ring_rotate(struct reagl_state_ring *ring)
{
	u32 *green;
	u32 *light;
	wb_t *old;

	green = ring->green[0];
	ring->green[0] = ring->green[1];
	ring->green[1] = ring->green[2];
	ring->green[2] = green;

	light = ring->light[0];
	ring->light[0] = ring->light[1];
	ring->light[1] = ring->light[2];
	ring->light[2] = light;

	old = ring->old[0];
	ring->old[0] = ring->old[1];
	ring->old[1] = ring->old[2];
	ring->old[2] = old;
}

static inline unsigned int reagl_ring_light_count(
					const struct reagl_state_ring *ring,
					unsigned long x)
{
	return ring->light[1][x - 1] + ring->light[0][x] +
		ring->light[1][x + 1] + ring->light[2][x];
}

static inline bool reagl_ring_all_green(
					const struct reagl_state_ring *ring,
					unsigned long x)
{
	return ring->green[0][x] && ring->green[1][x - 1] &&
		ring->green[1][x + 1] && ring->green[2][x];
}

static inline u32 reagl_active_value(unsigned long x, unsigned long y)
{
	if (!activeMaskMatrix)
		return 0;
	return activeMaskMatrix[(x & 7) | ((y & 7) << 3)];
}

static void reagl_copy_full_frame(wb_t *out, const wb_t *in,
					  long frame_width, long frame_height)
{
	if (out && in && out != in && frame_width > 0 && frame_height > 0)
		memcpy(out, in, (unsigned long)frame_width * frame_height *
			sizeof(wb_t));
}

/* Freescale v2.2.1: stateful four-neighbour processing into buffer B. */
static void reagl_process_v2_2_1(const wb_t *input, wb_t *output,
					 const struct reagl_region *region,
					 long frame_width, long frame_height)
{
	struct reagl_state_ring ring;
	unsigned long row;
	unsigned long x;
	unsigned long top;
	unsigned long left;
	unsigned long width;
	unsigned long height;

	if (!input || !output || !region || !nextWhite || !currWhite ||
		!workingLines || frame_width <= 0 || frame_height <= 0)
		return;

	reagl_copy_full_frame(output, input, frame_width, frame_height);
	if (region->width < 3 || region->height < 3)
		return;

	top = region->top;
	left = region->left;
	width = region->width;
	height = region->height;
	reagl_ring_bind(&ring, frame_width);

	for (row = 0; row < REAGL_LINE_COUNT; row++) {
		reagl_ring_copy_old(&ring, row, input, top + row);
		reagl_ring_classify(&ring, row, input, top + row, left, width);
	}

	for (row = 1; row + 1 < height; row++) {
		for (x = left + 1; x < left + width - 1; x++) {
			unsigned int light_neighbours;
			wb_t *pixel;

			if (!ring.light[1][x])
				continue;
			pixel = &ring.old[1][x];
			light_neighbours = reagl_ring_light_count(&ring, x);
			if (light_neighbours <= 1) {
				*pixel = reagl_gray(*pixel);
			} else if (light_neighbours < 4 &&
					reagl_ring_all_green(&ring, x)) {
				*pixel = reagl_white(*pixel);
			}
		}

		memcpy(output + (top + row) * ring.frame_width,
			ring.old[1], ring.frame_width * sizeof(wb_t));
		if (row + 1 < height - 1) {
			reagl_ring_rotate(&ring);
			reagl_ring_copy_old(&ring, 2, input, top + row + 2);
			reagl_ring_classify(&ring, 2, input, top + row + 2,
						left, width);
		}
	}
}

/* REAGL-D's active-mask decision, including its persistent update phase. */
static void reagl_process_reagld(const wb_t *input, wb_t *output,
					 const struct reagl_region *region,
					 long frame_width, long frame_height)
{
	struct reagl_state_ring ring;
	unsigned long row;
	unsigned long x;
	unsigned long top;
	unsigned long left;
	unsigned long width;
	unsigned long height;
	unsigned int phase;

	if (!input || !output || !region || !nextWhite || !currWhite ||
		!workingLines || !activeMaskMatrix || frame_width <= 0 ||
		frame_height <= 0 || !region->width || !region->height)
		return;

	reagl_copy_full_frame(output, input, frame_width, frame_height);
	memcpy(activeMaskMatrix, activeMask, sizeof(activeMask));

	top = region->top;
	left = region->left;
	width = region->width;
	height = region->height;
	phase = reaGLD_update_counter & 7;
	reagl_ring_bind(&ring, frame_width);
	reagl_ring_clear_line(&ring, 0);
	reagl_ring_classify(&ring, 1, input, top, left, width);

	for (row = 0; row < height; row++) {
		wb_t *pixel;
		unsigned long absolute_x;

		memcpy(workingLines, input + (top + row) * ring.frame_width,
			ring.frame_width * sizeof(wb_t));
		if (row + 1 < height)
			reagl_ring_classify(&ring, 2, input, top + row + 1,
						left, width);
		else
			reagl_ring_clear_line(&ring, 2);

		for (x = 0; x < width; x++) {
			unsigned int light_neighbours;
			bool white = false;
			unsigned long active_row = row & 7;
			unsigned int active_index;

			absolute_x = left + x;
			if (!ring.light[1][absolute_x])
				continue;

			pixel = &workingLines[absolute_x];
			light_neighbours = reagl_ring_light_count(&ring, absolute_x);
			active_index = (x & 7) | (active_row << 3);
			if (light_neighbours <= 1 ||
				activeMaskMatrix[active_index] == phase) {
				*pixel = reagl_gray(*pixel);
				continue;
			}

			if (!reagl_ring_all_green(&ring, absolute_x))
				continue;
			if (light_neighbours < 4) {
				white = true;
			} else if (reagl_active_value(x, row - 1) == phase ||
					reagl_active_value(x - 1, row) == phase ||
					reagl_active_value(x + 1, row) == phase ||
					reagl_active_value(x, row + 1) == phase) {
				white = true;
			}
			if (white)
				*pixel = reagl_white(*pixel);
		}

		memcpy(output + (top + row) * ring.frame_width,
			workingLines, ring.frame_width * sizeof(wb_t));
		reagl_ring_rotate(&ring);
	}
}

int reagl_init(long working_buffer_width)
{
	unsigned long line_words;
	unsigned long working_words;

	line_words = ((unsigned long)working_buffer_width +
		REAGL_LINE_PADDING) * REAGL_LINE_COUNT;
	working_words = (unsigned long)working_buffer_width * REAGL_LINE_COUNT;
	reaGLD_update_counter = 0;

	nextWhite = kmalloc(line_words * sizeof(*nextWhite), GFP_KERNEL);
	if (!nextWhite)
		return -1;
	currWhite = kmalloc(line_words * sizeof(*currWhite), GFP_KERNEL);
	if (!currWhite)
		return -1;
	if (!kmalloc_caches[11]) {
		/* Preserve the original's odd cache-missing fallback. */
		activeMaskMatrix = (u32 *)0x10;
	} else {
		activeMaskMatrix = kmem_cache_alloc(kmalloc_caches[11],
							GFP_KERNEL);
		if (!activeMaskMatrix)
			return -1;
	}
	workingLines = kmalloc(working_words * sizeof(*workingLines), GFP_KERNEL);
	if (!workingLines)
		return -1;
	return 0;
}
EXPORT_SYMBOL(reagl_init);

void reagl_free(void)
{
	if (nextWhite)
		kfree(nextWhite);
	if (currWhite)
		kfree(currWhite);
	if (activeMaskMatrix)
		kfree(activeMaskMatrix);
	if (workingLines)
		kfree(workingLines);
}
EXPORT_SYMBOL(reagl_free);

void do_reagl_processing_v_2_lab126(wb_t *working_buffer_ptr_in,
					wb_t *working_buffer_ptr_out,
					struct mxcfb_rect *update_region,
					long working_buffer_width,
					long working_buffer_height)
{
	struct reagl_region region;

	/* The original v2 implementation processes argument 2 in place. */
	if (!reagl_make_region(&region, update_region,
				working_buffer_width, working_buffer_height))
		return;
	(void)working_buffer_ptr_in;
	reagl_process_v2(working_buffer_ptr_out, &region,
				working_buffer_width, working_buffer_height);
}
EXPORT_SYMBOL(do_reagl_processing_v_2_lab126);

void do_reagl_processing_v_4_lab126(wb_t *working_buffer_ptr_in,
					wb_t *working_buffer_ptr_out,
					struct mxcfb_rect *update_region,
					long working_buffer_width,
					long working_buffer_height)
{
	struct reagl_region region;

	reagl_flush_caches();
	if (!reagl_make_region(&region, update_region,
				working_buffer_width, working_buffer_height))
		return;
	reagl_process_v4(working_buffer_ptr_out, &region,
				working_buffer_width, working_buffer_height);
}
EXPORT_SYMBOL(do_reagl_processing_v_4_lab126);

void do_reagl_processing_v2_2_1(wb_t *working_buffer_ptr_in,
					wb_t *working_buffer_ptr_out,
					struct mxcfb_rect *update_region,
					long working_buffer_width,
					long working_buffer_height)
{
	struct reagl_region region;

	reagl_flush_caches();
	if (!reagl_make_region(&region, update_region,
				working_buffer_width, working_buffer_height))
		return;
	reagl_process_v2_2_1(working_buffer_ptr_in, working_buffer_ptr_out,
					 &region,
					working_buffer_width, working_buffer_height);
}
EXPORT_SYMBOL(do_reagl_processing_v2_2_1);

void do_reagld_processing_v2_0_1(wb_t *working_buffer_ptr_in,
					 wb_t *working_buffer_ptr_out,
					 struct mxcfb_rect *update_region,
					 long working_buffer_width,
					 long working_buffer_height)
{
	struct reagl_region region;

	reagl_flush_caches();
	if (!reagl_make_region(&region, update_region,
				working_buffer_width, working_buffer_height))
		return;
	reagl_process_reagld(working_buffer_ptr_in, working_buffer_ptr_out,
					 &region,
					working_buffer_width, working_buffer_height);

	/* The original stores this as a 16-bit phase counter. */
	reaGLD_update_counter = (u16)(reaGLD_update_counter + 1);
}
EXPORT_SYMBOL(do_reagld_processing_v2_0_1);

MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("MXC EPDC framebuffer driver");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
