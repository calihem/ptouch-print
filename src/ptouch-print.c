/*
	ptouch-print - Print labels with images or text on a Brother P-Touch

	Copyright (C) 2015-2025 Dominic Radermacher <dominic@familie-radermacher.ch>

	This program is free software; you can redistribute it and/or modify it
	under the terms of the GNU General Public License version 3 as
	published by the Free Software Foundation

	This program is distributed in the hope that it will be useful, but
	WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software Foundation,
	Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <argp.h>
#include <stdio.h>	/* printf() */
#include <stdlib.h>	/* exit(), malloc() */
#include <stdbool.h>
#include <string.h>	/* strcmp(), memcmp() */
#include <sys/types.h>	/* open() */
#include <sys/stat.h>	/* open() */
#include <fcntl.h>	/* open() */
#include <gd.h>
#include <libintl.h>
#include <locale.h>	/* LC_ALL */

#include "version.h"
#include "gettext.h"	/* gettext(), ngettext() */
#include "ptouch.h"

#define _(s) gettext(s)

#define MAX_LINES 4	/* maybe this should depend on tape size */

#define P_NAME "ptouch-print"

struct arguments {
	bool chain;
	int copies;
	bool debug;
	bool info;
	char *font_file;
	int font_size;
	int forced_tape_width;
	char *save_png;
	int verbose;
};

typedef enum { JOB_CUTMARK, JOB_IMAGE, JOB_PAD, JOB_TEXT, JOB_UNDEFINED } job_type_t;

typedef struct job {
	job_type_t type;
	int n;
	char *lines[MAX_LINES];
	struct job *next;
} job_t;

gdImage *image_load(const char *file);
void rasterline_setpixel(uint8_t* rasterline, size_t size, int pixel);
int get_baselineoffset(char *text, char *font, int fsz);
int find_fontsize(int want_px, char *font, char *text);
int needed_width(char *text, char *font, int fsz);
int print_img(ptouch_dev ptdev, gdImage *im, int chain);
int write_png(gdImage *im, const char *file);
gdImage *img_append(gdImage *in_1, gdImage *in_2);
gdImage *img_cutmark(int print_width);
gdImage *render_text(char *font, char *line[], int lines, int print_width);
void unsupported_printer(ptouch_dev ptdev);
void add_job(job_type_t type, int n, char *line);
static error_t parse_opt(int key, char *arg, struct argp_state *state);

const char *argp_program_version = P_NAME " " VERSION;
const char *argp_program_bug_address = "Dominic Radermacher <dominic@familie-radermacher.ch>";
static char doc[] = "ptouch-print is a command line tool to print labels on Brother P-Touch printers on Linux.";
static char args_doc[] = "";

static struct argp_option options[] = {
	// name, key, arg, flags, doc, group
	{ 0, 0, 0, 0, _("options:"), 1},
	{ "debug", 1, 0, 0, _("Enable debug output"), 1},
	{ "font", 2, "<file>", 0, _("Use font <file> or <name>"), 1},
	{ "fontsize", 3, "<size>", 0, _("Manually set font size"), 1},
	{ "writepng", 4, "<file>", 0, _("Instead of printing, write output to png <file>"), 1},
	{ "force-tape-width", 5, "<px>", 0, _("Set tape width in pixels, use together with --writepng without a printer connected"), 1},
	{ "copies", 6, "<number>", 0, _("Sets the number of identical prints"), 1},

	{ 0, 0, 0, 0, _("print commands:"), 2},
	{ "image", 'i', "<file>", 0, _("Print the given image which must be a 2 color (black/white) png"), 2},
	{ "text", 't', "<text>", 0, _("Print line of <text>. If the text contains spaces, use quotation marks taround it"), 2},
	{ "cutmark", 'c', 0, 0, _("Print a mark where the tape should be cut"), 2},
	{ "pad", 'p', "<n>", 0, _("Add n pixels padding (blank tape)"), 2},
	{ "chain", 10, 0, 0, _("Skip final feed of label and any automatic cut"), 2},
	{ "newline", 'n', "<line>", 0, "Add 1-4 lines for multiline text", 2},
	
	{ 0, 0, 0, 0, _("other commands:"), 3},
	{ "info", 20, 0, 0, _("Show info about detected tape"), 3},
	{ "list-supported", 21, 0, 0, _("Show printers supported by this version"), 3},
	{ 0 }
};

static struct argp argp = { options, parse_opt, args_doc, doc, 0, 0, 0 };

struct arguments arguments = {
	.chain =false,
	.copies = 1,
	.debug = false,
	.info = false,
	//.font_file = "/usr/share/fonts/TTF/Ubuntu-M.ttf",
	//.font_file = "Ubuntu:medium",
	.font_file = "DejaVuSans",
	.font_size = 0,
	.forced_tape_width = 0,
	.save_png = NULL,
	.verbose = 0
};

job_t *jobs = NULL;
job_t *last_added_job = NULL;

/* --------------------------------------------------------------------
   -------------------------------------------------------------------- */

void rasterline_setpixel(uint8_t* rasterline, size_t size, int pixel)
{
//	TODO: pixel should be unsigned, since we can't have negative
//	if (pixel > ptdev->devinfo->device_max_px) {
	if ((pixel < 0) || (pixel >= (int)(size*8))) {
		return;
	}
	rasterline[(size-1)-(pixel/8)] |= (uint8_t)(1<<(pixel%8));
	return;
}

int print_img(ptouch_dev ptdev, gdImage *im, int chain)
{
	uint8_t rasterline[(ptdev->devinfo->max_px)/8];

	if (!im) {
		printf(_("nothing to print\n"));
		return -1;
	}
	int tape_width = ptouch_get_tape_width(ptdev);
	size_t max_pixels = ptouch_get_max_width(ptdev);
	/* find out whether color 0 or color 1 is darker */
	int d = (gdImageRed(im,1) + gdImageGreen(im,1) + gdImageBlue(im,1) < gdImageRed(im,0) + gdImageGreen(im,0) + gdImageBlue(im,0))?1:0;
	if (gdImageSY(im) > tape_width) {
		printf(_("image is too large (%ipx x %ipx)\n"), gdImageSX(im), gdImageSY(im));
		printf(_("maximum printing width for this tape is %ipx\n"), tape_width);
		return -1;
	}
	printf(_("image size (%ipx x %ipx)\n"), gdImageSX(im), gdImageSY(im));
	int offset = ((int)max_pixels / 2) - (gdImageSY(im)/2);	/* always print centered */
	printf("max_pixels=%ld, offset=%d\n", max_pixels, offset);
	if ((ptdev->devinfo->flags & FLAG_RASTER_PACKBITS) == FLAG_RASTER_PACKBITS) {
		if (arguments.debug) {
			printf("enable PackBits mode\n");
		}
		ptouch_enable_packbits(ptdev);
	}
	if (ptouch_rasterstart(ptdev) != 0) {
		printf(_("ptouch_rasterstart() failed\n"));
		return -1;
	}
	if ((ptdev->devinfo->flags & FLAG_USE_INFO_CMD) == FLAG_USE_INFO_CMD) {
		ptouch_info_cmd(ptdev, gdImageSX(im));
		if (arguments.debug) {
			printf(_("send print information command\n"));
		}
	}
	if ((ptdev->devinfo->flags & FLAG_D460BT_MAGIC) == FLAG_D460BT_MAGIC) {
		if (chain) {
			ptouch_send_d460bt_chain(ptdev);
			if (arguments.debug) {
				printf(_("send PT-D460BT chain commands\n"));
			}
		}
		ptouch_send_d460bt_magic(ptdev);
		if (arguments.debug) {
			printf(_("send PT-D460BT magic commands\n"));
		}
	}
	if ((ptdev->devinfo->flags & FLAG_HAS_PRECUT) == FLAG_HAS_PRECUT) {
		ptouch_send_precut_cmd(ptdev, 1);
		if (arguments.debug) {
			printf(_("send precut command\n"));
		}
	}
	for (int k = 0; k < gdImageSX(im); ++k) {
		memset(rasterline, 0, sizeof(rasterline));
		for (int i = 0; i < gdImageSY(im); ++i) {
			if (gdImageGetPixel(im, k, gdImageSY(im) - 1 - i) == d) {
				rasterline_setpixel(rasterline, sizeof(rasterline), offset+i);
			}
		}
		if (ptouch_sendraster(ptdev, rasterline, (ptdev->devinfo->max_px / 8)) != 0) {
			printf(_("ptouch_sendraster() failed\n"));
			return -1;
		}
	}
	return 0;
}

/* --------------------------------------------------------------------
	Function	image_load()
	Description	detect the type of a image and try to load it
	Last update	2005-10-16
	Status		Working, should add debug info
   -------------------------------------------------------------------- */

gdImage *image_load(const char *file)
{
	const uint8_t png[8] = {0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
	char d[10];
	FILE *f;
	gdImage *img = NULL;

	if (!strcmp(file, "-")) {
		f = stdin;
	} else {
		f = fopen(file, "rb");
	}
	if (f == NULL) {	/* error could not open file */
		return NULL;
	}
	if (fseek(f, 0L, SEEK_SET)) {	/* file is not seekable. eg 'stdin' */
		img = gdImageCreateFromPng(f);
	} else {
		if (fread(d, sizeof(d), 1, f) != 1) {
			return NULL;
		}
		rewind(f);
		if (memcmp(d, png, 8) == 0) {
			img = gdImageCreateFromPng(f);
		}
	}
	fclose(f);
	return img;
}

int write_png(gdImage *im, const char *file)
{
	FILE *f;

	if ((f = fopen(file, "wb")) == NULL) {
		printf(_("writing image '%s' failed\n"), file);
		return -1;
	}
	gdImagePng(im, f);
	fclose(f);
	return 0;
}

/* --------------------------------------------------------------------
	Find out the difference in pixels between a "normal" char and one
	that goes below the font baseline
   -------------------------------------------------------------------- */
int get_baselineoffset(char *text, char *font, int fsz)
{
	int brect[8];

	/* NOTE: This assumes that 'o' is always on the baseline */
	gdImageStringFT(NULL, &brect[0], -1, font, fsz, 0.0, 0, 0, "o");
	int o_offset = brect[1];
	gdImageStringFT(NULL, &brect[0], -1, font, fsz, 0.0, 0, 0, text);
	int text_offset = brect[1];
	if (arguments.debug) {
		printf(_("debug: o baseline offset - %d\n"), o_offset);
		printf(_("debug: text baseline offset - %d\n"), text_offset);
	}
	return text_offset-o_offset;
}

/* --------------------------------------------------------------------
	Find out which fontsize we need for a given font to get a
	specified pixel size
	NOTE: This does NOT work for some UTF-8 chars like µ
   -------------------------------------------------------------------- */
int find_fontsize(int want_px, char *font, char *text)
{
	int save = 0;
	int brect[8];

	for (int i=4; ; ++i) {
		if (gdImageStringFT(NULL, &brect[0], -1, font, i, 0.0, 0, 0, text) != NULL) {
			break;
		}
		if (brect[1]-brect[5] <= want_px) {
			save = i;
		} else {
			break;
		}
	}
	if (save == 0) {
		return -1;
	}
	return save;
}

int needed_width(char *text, char *font, int fsz)
{
	int brect[8];

	if (gdImageStringFT(NULL, &brect[0], -1, font, fsz, 0.0, 0, 0, text) != NULL) {
		return -1;
	}
	return brect[2]-brect[0];
}

int offset_x(char *text, char *font, int fsz)
{
	int brect[8];

	if (gdImageStringFT(NULL, &brect[0], -1, font, fsz, 0.0, 0, 0, text) != NULL) {
		return -1;
	}
	return -brect[0];
}

gdImage *render_text(char *font, char *line[], int lines, int print_width)
{
	int brect[8];
	int i, black, x = 0, tmp = 0, fsz = 0;
	char *p;
	gdImage *im = NULL;

	if (arguments.debug) {
		printf(_("render_text(): %i lines, font = '%s'\n"), lines, font);
	}
	if (gdFTUseFontConfig(1) != GD_TRUE) {
		printf(_("warning: font config not available\n"));
	}
	if (arguments.font_size > 0) {
		fsz = arguments.font_size;
		printf(_("setting font size=%i\n"), fsz);
	} else {
		for (i = 0; i < lines; ++i) {
			if ((tmp = find_fontsize(print_width/lines, font, line[i])) < 0) {
				printf(_("could not estimate needed font size\n"));
				return NULL;
			}
			if ((fsz == 0) || (tmp < fsz)) {
				fsz=tmp;
			}
		}
		printf(_("choosing font size=%i\n"), fsz);
	}
	for(i = 0; i < lines; ++i) {
		tmp = needed_width(line[i], arguments.font_file, fsz);
		if (tmp > x) {
			x = tmp;
		}
	}
	im = gdImageCreatePalette(x, print_width);
	gdImageColorAllocate(im, 255, 255, 255);
	black = gdImageColorAllocate(im, 0, 0, 0);
	/* gdImageStringFT(im,brect,fg,fontlist,size,angle,x,y,string) */
	/* find max needed line height for ALL lines */
	int max_height=0;
	for (i = 0; i < lines; ++i) {
		if ((p = gdImageStringFT(NULL, &brect[0], -black, font, fsz, 0.0, 0, 0, line[i])) != NULL) {
			printf(_("error in gdImageStringFT: %s\n"), p);
		}
		//int ofs = get_baselineoffset(line[i], font_file, fsz);
		int lineheight = brect[1]-brect[5];
		if (lineheight > max_height) {
			max_height = lineheight;
		}
	}
	if (arguments.debug) {
		printf("debug: needed (max) height is %ipx\n", max_height);
	}
	if ((max_height * lines) > print_width) {
		printf("Font size %d too large for %d lines\n", fsz, lines);
		return NULL;
	}
	/* calculate unused pixels */
	int unused_px = print_width - (max_height * lines);
	/* now render lines */
	for (i = 0; i < lines; ++i) {
		int ofs = get_baselineoffset(line[i], arguments.font_file, fsz);
		//int pos = ((i)*(print_width/(lines)))+(max_height)-ofs-1;
		int pos = ((i)*(print_width/(lines)))+(max_height)-ofs;
		pos += (unused_px/lines) / 2;
		if (arguments.debug) {
			printf("debug: line %i pos=%i ofs=%i\n", i+1, pos, ofs);
		}
		int off_x = offset_x(line[i], arguments.font_file, fsz);
		if ((p = gdImageStringFT(im, &brect[0], -black, font, fsz, 0.0, off_x, pos, line[i])) != NULL) {
			printf(_("error in gdImageStringFT: %s\n"), p);
		}
	}
	return im;
}

gdImage *img_append(gdImage *in_1, gdImage *in_2)
{
	gdImage *out = NULL;
	int width = 0;
	int i_1_x = 0;
	int length = 0;

	if (in_1 != NULL) {
		width = gdImageSY(in_1);
		length = gdImageSX(in_1);
		i_1_x = gdImageSX(in_1);
	}
	if (in_2 != NULL) {
		length += gdImageSX(in_2);
		/* width should be the same, but let's be sure */
		if (gdImageSY(in_2) > width) {
			width = gdImageSY(in_2);
		}
	}
	if ((width == 0) || (length == 0)) {
		return NULL;
	}
	out = gdImageCreatePalette(length, width);
	if (out == NULL) {
		return NULL;
	}
	gdImageColorAllocate(out, 255, 255, 255);
	gdImageColorAllocate(out, 0, 0, 0);
	if (arguments.debug) {
		printf("debug: created new img with size %d * %d\n", length, width);
	}
	if (in_1 != NULL) {
		gdImageCopy(out, in_1, 0, 0, 0, 0, gdImageSX(in_1), gdImageSY(in_1));
		if (arguments.debug) {
			printf("debug: copied part 1\n");
		}
	}
	if (in_2 != NULL) {
		gdImageCopy(out, in_2, i_1_x, 0, 0, 0, gdImageSX(in_2), gdImageSY(in_2));
		if (arguments.debug) {
			printf("copied part 2\n");
		}
	}
	return out;
}

gdImage *img_cutmark(int print_width)
{
	gdImage *out = NULL;
	int style_dashed[6];

	out = gdImageCreatePalette(9, print_width);
	if (out == NULL) {
		return NULL;
	}
	gdImageColorAllocate(out, 255, 255, 255);
	int black = gdImageColorAllocate(out, 0, 0, 0);
	style_dashed[0] = gdTransparent;
	style_dashed[1] = gdTransparent;
	style_dashed[2] = gdTransparent;
	style_dashed[3] = black;
	style_dashed[4] = black;
	style_dashed[5] = black;
	gdImageSetStyle(out, style_dashed, 6);
	gdImageLine(out, 5, 0, 5, print_width - 1, gdStyled);
	return out;
}

gdImage *img_padding(int print_width, int length)
{
	gdImage *out = NULL;

	if ((length < 1) || (length > 256)) {
		length=1;
	}
	out = gdImageCreatePalette(length, print_width);
	if (out == NULL) {
		return NULL;
	}
	gdImageColorAllocate(out, 255, 255, 255);
	return out;
}

void add_job(job_type_t type, int n, char *line)
{
	job_t *new_job = (job_t*)malloc(sizeof(job_t));
	if(!new_job) {
		fprintf(stderr, "Memory allocation failed\n"); 
		return;
	}

	new_job->type = type;
	if(type == JOB_TEXT && n > MAX_LINES) {
		n = MAX_LINES;
	}
	new_job->n = n;
	new_job->lines[0] = line;
	for(int i=1; i<MAX_LINES; i++)
		new_job->lines[i] = NULL;
	new_job->next = NULL;

	if(!last_added_job) { // just created the first job
		jobs = last_added_job = new_job;
		return;
	}

	last_added_job->next = new_job;
	last_added_job = new_job;
}

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct arguments *arguments = (struct arguments *)state->input;

	switch (key) {
		case 1: // debug
			arguments->debug = true;
			break;
		case 2: // font
			arguments->font_file = arg;
			break;
		case 3: // fontsize
			arguments->font_size = strtol(arg, NULL, 10);
			break;
		case 4: // writepng
			arguments->save_png = arg;
			break;
		case 5: // force-tape-width
			arguments->forced_tape_width = strtol(arg, NULL, 10);
			break;
		case 6: // copies
			arguments->copies = strtol(arg, NULL, 10);
			break;
		case 'i': // image
			add_job(JOB_IMAGE, 1, arg);
			 break;
		case 't': // text
			add_job(JOB_TEXT, 1, arg);
			 break;
		case 'c': // cutmark
			add_job(JOB_CUTMARK, 0, NULL);
			break;
		case 'p': // pad
			add_job(JOB_PAD, atoi(arg), NULL);
			break;
		case 10: // chain
			arguments->chain = true;
			break;
		case 'n': // newline
			if(!last_added_job || last_added_job->type != JOB_TEXT) {
				add_job(JOB_TEXT, 1, arg);
				break;
			}

			if(last_added_job->n >= MAX_LINES) { // max number of lines reached
				argp_failure(state, 1, EINVAL, _("Only up to %d lines are supported"), MAX_LINES);
				break;
			}

			last_added_job->lines[last_added_job->n++] = arg;
			break;
		case 20: // info
			arguments->info = true;
			break;
		case 21: // list-supported
			ptouch_list_supported();
			exit(0);
		case ARGP_KEY_ARG:
			argp_failure(state, 1, E2BIG, _("No arguments supported"));
			break;
		case ARGP_KEY_END:
			// final argument validation
			if (arguments->forced_tape_width && !arguments->save_png) {
				argp_failure(state, 1, ENOTSUP, _("Option --writepng missing"));
			}
			if (arguments->forced_tape_width && arguments->info) {
				argp_failure(state, 1, ENOTSUP, _("Options --force_tape_width and --info can't be used together"));
			}
			break;
		default:
			return ARGP_ERR_UNKNOWN;
	}
	
	return 0;
}

int main(int argc, char *argv[])
{
	int print_width = 0;
	gdImage *im = NULL;
	gdImage *out = NULL;
	ptouch_dev ptdev = NULL;

	setlocale(LC_ALL, "");
	const char *textdomain_dir = getenv("TEXTDOMAINDIR");
	if (!textdomain_dir) {
		textdomain_dir = "/usr/share/locale/";
	}
	bindtextdomain(P_NAME, "/usr/share/locale/");
	textdomain(P_NAME);

	argp_parse(&argp, argc, argv, 0, 0, &arguments);

	if (!arguments.forced_tape_width) {
		if ((ptouch_open(&ptdev)) < 0) {
			return 5;
		}
		if (ptouch_init(ptdev) != 0) {
			printf(_("ptouch_init() failed\n"));
		}
		if (ptouch_getstatus(ptdev) != 0) {
			printf(_("ptouch_getstatus() failed\n"));
			return 1;
		}
		print_width = ptouch_get_tape_width(ptdev);
		int max_print_width = ptouch_get_max_width(ptdev);
		// do not try to print more pixels than printhead has
		if (print_width > max_print_width) {
			print_width = max_print_width;
		}
	} else {	// --forced_tape_width together with --writepng
		print_width = arguments.forced_tape_width;
	}

	if(arguments.info) {
		printf(_("maximum printing width for this printer is %ldpx\n"), ptouch_get_max_width(ptdev));
		printf(_("maximum printing width for this tape is %ldpx\n"), ptouch_get_tape_width(ptdev));
		printf("media type = %02x (%s)\n", ptdev->status->media_type, pt_mediatype(ptdev->status->media_type));
		printf("media width = %d mm\n", ptdev->status->media_width);
		printf("tape color = %02x (%s)\n", ptdev->status->tape_color, pt_tapecolor(ptdev->status->tape_color));
		printf("text color = %02x (%s)\n", ptdev->status->text_color, pt_textcolor(ptdev->status->text_color));
		printf("error = %04x\n", ptdev->status->error);
		if (arguments.debug) {
			ptouch_rawstatus((uint8_t *)ptdev->status);
		}
		exit(0);
	}

	// iterate through all print jobs	
	for(job_t *job = jobs; job != NULL; job = job->next) {
		if(arguments.debug) {
			printf("job %p: type=%d | n=%d", job, job->type, job->n);
			for(int i=0; i<MAX_LINES; i++)
				printf(" | %s", job->lines[i]);
			printf(" | next=%p\n", job->next);
		}

		switch(job->type) {
			case JOB_IMAGE:
				if ((im = image_load(job->lines[0])) == NULL) {
					printf(_("failed to load image file\n"));
					return 1;
				}
				out = img_append(out, im);
				gdImageDestroy(im);
				im = NULL;
				break;
			case JOB_TEXT:
				if ((im = render_text(arguments.font_file, job->lines, job->n, print_width)) == NULL) {
					printf(_("could not render text\n"));
					return 1;
				}
				out = img_append(out, im);
				gdImageDestroy(im);
				im = NULL;
				break;
			case JOB_CUTMARK:
				im = img_cutmark(print_width);
				out = img_append(out, im);
				gdImageDestroy(im);
				im = NULL;
				break;
			case JOB_PAD:
				im = img_padding(print_width, job->n);
				out = img_append(out, im);
				gdImageDestroy(im);
				im = NULL;
				break;
			default:
				break;
		}
	}

	// clean up job list
	for(job_t *job = jobs; job != NULL; ) {
		job_t *next = job->next;
		free(job);
		job = next;
	}
	jobs = last_added_job = NULL;

	if (out) {
		if (arguments.save_png) {
			write_png(out, arguments.save_png);
		} else {
			for (int i = 0; i < arguments.copies; ++i) {
				print_img(ptdev, out, arguments.chain);
				if (ptouch_finalize(ptdev, ( arguments.chain || (i < arguments.copies-1) ) ) != 0) {
					printf(_("ptouch_finalize(%d) failed\n"), arguments.chain);
					return 2;
				}
			}
		}
		gdImageDestroy(out);
	}
	if (im != NULL) {
		gdImageDestroy(im);
	}
	if (!arguments.forced_tape_width) {
		ptouch_close(ptdev);
	}
	libusb_exit(NULL);
	return 0;
}
