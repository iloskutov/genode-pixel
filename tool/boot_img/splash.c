/*
 * \brief  Boot splash of the trampoline (Google Pixel 7)
 * \author Ivan Loskutov
 * \date   2026-11-15
 *
 * The splash is a band of 1080 x 800 pixels (0x00RRGGBB, little endian,
 * row by row) for the middle third of the 1080 x 2400 screen, followed by
 * the colour of the rest of the screen (the same format) and the 8 bytes
 * "MuesliOS". It is the ramdisk of the init_boot partition
 * (tool/splash/mk_splash). The boot loader loads the ramdisks one after
 * the other and appends the bootconfig, so the image ends right before the
 * bootconfig.
 *
 * Nothing of the image is interpreted, as the partition can be written
 * from outside: the magic is only compared, the colours are only copied,
 * size, position, and format are fixed here, and the image is found only
 * with what the boot loader passes (the ramdisk range in the device
 * tree's /chosen node, the bootconfig's size), and it
 * is drawn only if it lies within the ramdisk range and the display is in
 * the expected state: DECON0 enabled, DPP layer 0 scanning out 1080 x 2400
 * in a 32-bit format from the reserved framebuffer at 0xfac00000. Wrong
 * data gives a wrong picture, nothing else.
 *
 * 'splash_find' runs with the MMU off (device memory: only aligned
 * accesses), 'splash_draw' with the MMU on, the image mapped as cached
 * memory, the framebuffer as normal non-cacheable memory, and the display
 * registers as device memory. The panel keeps the frame in its own memory,
 * and the display driver copies the framebuffer into its first frame.
 */

/*
 * SPDX-License-Identifier: MIT
 */

typedef unsigned char      u8;
typedef unsigned int       u32;
typedef unsigned long long u64;

#define WIDTH   1080
#define HEIGHT  2400
#define BAND    800                       /* rows of the band */
#define BAND_Y  ((HEIGHT - BAND) / 2)     /* first row of the band */
#define SIZE    (WIDTH * BAND * 4)        /* bytes of the band */
#define MAGIC   "MuesliOS"
#define TRAILER 12                        /* background colour, magic */

#define FB_BASE 0xfac00000ull
#define FB_SIZE 0xa00000ull

_Static_assert(WIDTH * HEIGHT * 4 <= FB_SIZE, "screen larger than the framebuffer");

/* DPP layer 0 RDMA */
#define DPP_BASE          0x1c0b0000ull
#define RDMA_IN_CTRL_0    0x08         /* IMG_FORMAT [13:8] */
#define RDMA_SRC_SIZE     0x10         /* height [31:16], width [15:0] */
#define RDMA_BASEADDR_Y8  0x40

/* DECON0 */
#define DECON_BASE        0x1c240000ull
#define GLOBAL_CON        0x20
#define DECON_ENABLED     0x3          /* DECON_EN_F | DECON_EN */
#define IDLE_STATUS       (1u << 5)
#define OP_MODE_COMMAND   (1u << 8)
#define TRIG_CON          0x30
#define HW_TRIG_EN        (1u << 0)
#define SW_TRIG_DET_EN    (1u << 1)
#define HW_TRIG_MASK      (1u << 4)
#define SW_TRIG_EN        (1u << 8)
#define SHD_REG_UP_REQ    0x50
#define SHD_GLOBAL        (1u << 31)
#define POLLS             1000000

/* flattened device tree */
#define FDT_MAGIC         0xd00dfeed
#define FDT_BEGIN_NODE    1
#define FDT_END_NODE      2
#define FDT_PROP          3
#define FDT_NOP           4


/*
 * What 'splash_find' passes to the trampoline and to 'splash_draw',
 * the offsets are used by trampoline.S
 */
struct Splash
{
	u64 image;        /* 0: address of the image, 0 if nothing is drawn */
	u64 size;         /* 8: SIZE */
	u32 format;       /* 16: the layer's pixel format */
	u32 global_con;   /* 20: DECON0's GLOBAL_CON */
	u32 background;   /* 24: colour of the rest of the screen */
};


static inline u32 read32(u64 addr) { return *(u32 volatile const *)addr; }

static inline void write32(u64 addr, u32 v) { *(u32 volatile *)addr = v; }

static inline u8 read8(u64 addr) { return *(u8 volatile const *)addr; }

static u32 be32(u64 a) {
	return (u32)read8(a) << 24 | (u32)read8(a + 1) << 16
	     | (u32)read8(a + 2) << 8 | read8(a + 3); }

static u32 le32(u64 a) {
	return (u32)read8(a + 3) << 24 | (u32)read8(a + 2) << 16
	     | (u32)read8(a + 1) << 8 | read8(a); }

static int equal(u64 a, char const *s, unsigned n)
{
	for (unsigned i = 0; i < n; i++)
		if (read8(a + i) != (u8)s[i])
			return 0;
	return 1;
}


/*
 * The ramdisk range from the device tree's /chosen node
 */
static int initrd(u64 dtb, u64 *start, u64 *end)
{
	if (be32(dtb) != FDT_MAGIC)
		return 0;

	u64 const strings = dtb + be32(dtb + 12);
	u64 p = dtb + be32(dtb + 8);
	unsigned depth = 0, chosen = 0, found = 0;

	for (;;) {
		u32 const token = be32(p);
		p += 4;

		if (token == FDT_BEGIN_NODE) {
			depth++;
			chosen = (depth == 2 && equal(p, "chosen", 7));
			while (read8(p)) p++;
			p = (p + 1 + 3) & ~3ull;

		} else if (token == FDT_END_NODE) {
			if (chosen || depth-- == 0) return 0;

		} else if (token == FDT_PROP) {
			u32 const len = be32(p), name = be32(p + 4);
			u64 const value = p + 8;
			p = (value + len + 3) & ~3ull;

			if (!chosen || (len != 4 && len != 8))
				continue;

			u64 const v = (len == 8) ? (u64)be32(value) << 32 | be32(value + 4)
			                         : be32(value);
			if (equal(strings + name, "linux,initrd-start", 19)) {
				*start = v; found |= 1; }
			if (equal(strings + name, "linux,initrd-end", 17)) {
				*end = v; found |= 2; }
			if (found == 3)
				return 1;

		} else if (token != FDT_NOP)
			return 0;
	}
}


/*
 * 0x00RRGGBB in the layer's 32-bit format, 0 for other formats
 *
 * A format's name is the byte order in memory, first byte first; the
 * boot loader uses 0 (BGRA).
 */
static inline int pack(unsigned format, u32 rgb, u32 *px)
{
	u32 const r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff,
	          a = 0xff;
	switch (format) {
	case 0: case 4: *px = a << 24 | r << 16 | g << 8 | b; return 1; /* BGRA, BGRX */
	case 1: case 5: *px = a << 24 | b << 16 | g << 8 | r; return 1; /* RGBA, RGBX */
	case 2: case 6: *px = r << 24 | g << 16 | b << 8 | a; return 1; /* ABGR, XBGR */
	case 3: case 7: *px = b << 24 | g << 16 | r << 8 | a; return 1; /* ARGB, XRGB */
	}
	return 0;
}


/*
 * Find the image and check the display, MMU off
 */
void splash_find(u64 dtb, struct Splash *s)
{
	s->image = 0;
	s->size  = SIZE;

	u64 start = 0, end = 0;
	if (!initrd(dtb, &start, &end) || end < start + 20)
		return;

	/* bootconfig: parameters, le32 size, le32 checksum, "#BOOTCONFIG\n" */
	if (equal(end - 12, "#BOOTCONFIG\n", 12)) {
		u64 const params = le32(end - 20);
		if (params > end - start - 20)
			return;
		end -= params + 20;
	}

	if (end - start < SIZE + TRAILER || !equal(end - 8, MAGIC, 8))
		return;
	end -= TRAILER;
	s->background = le32(end);

	u32 const size = read32(DPP_BASE + RDMA_SRC_SIZE);
	u32 px = 0;

	s->format     = (read32(DPP_BASE + RDMA_IN_CTRL_0) >> 8) & 0x3f;
	s->global_con = read32(DECON_BASE + GLOBAL_CON);

	if (!(s->global_con & DECON_ENABLED)
	 || !pack(s->format, 0, &px)
	 || read32(DPP_BASE + RDMA_BASEADDR_Y8) != FB_BASE
	 || (size & 0xffff) != WIDTH || (size >> 16) != HEIGHT)
		return;

	s->image = end - SIZE;
}


/*
 * Copy the image into the framebuffer and show one frame, MMU on
 */
void splash_draw(struct Splash const *s)
{
	typedef u32 u32_unaligned __attribute__((aligned(1)));

	u32_unaligned const *band = (u32_unaligned const *)s->image;
	u32 *fb = (u32 *)FB_BASE;

	u32 background = 0;
	pack(s->format, s->background, &background);

	for (unsigned i = 0; i < WIDTH * BAND_Y; i++)
		fb[i] = background;

	fb += WIDTH * BAND_Y;
	for (unsigned i = 0; i < WIDTH * BAND; i++)
		pack(s->format, band[i], &fb[i]);

	fb += WIDTH * BAND;
	for (unsigned i = 0; i < WIDTH * (HEIGHT - BAND_Y - BAND); i++)
		fb[i] = background;

	/* the pixels in memory before the display reads them */
	__asm__ volatile ("dsb sy" ::: "memory");

	/*
	 * The boot loader's last frame may still go to the panel, which then
	 * ignores the trigger: wait until DECON0 is idle, as Linux does
	 */
	for (unsigned i = 0; i < POLLS && !(read32(DECON_BASE + GLOBAL_CON) & IDLE_STATUS); i++);

	/* one command-mode frame: shadow update, unmask the trigger, wait, mask */
	write32(DECON_BASE + SHD_REG_UP_REQ, SHD_GLOBAL);

	if (!(s->global_con & OP_MODE_COMMAND))
		return;

	u32 const trig = read32(DECON_BASE + TRIG_CON);
	write32(DECON_BASE + TRIG_CON, (trig & HW_TRIG_EN)
	                               ? (trig & ~HW_TRIG_MASK)
	                               : (trig | SW_TRIG_EN | SW_TRIG_DET_EN));

	for (unsigned i = 0; i < POLLS && read32(DECON_BASE + SHD_REG_UP_REQ); i++);

	write32(DECON_BASE + TRIG_CON, trig);
}
