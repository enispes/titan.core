/*	$NetBSD: read.c,v 1.55 2010/03/22 22:59:06 christos Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christos Zoulas of Cornell University.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"
#if !defined(lint) && !defined(SCCSID)
#if 0
static char sccsid[] = "@(#)read.c	8.1 (Berkeley) 6/4/93";
#else
__RCSID("$NetBSD: read.c,v 1.55 2010/03/22 22:59:06 christos Exp $");
#endif
#endif /* not lint && not SCCSID */

/*
 * read.c: Clean this junk up! This is horrible code.
 *	   Terminal read functions
 */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include "el.h"

#define	OKCMD	-1	/* must be -1! */

private int	read__fixio(int, int);
private int	read_preread(EditLine *);
private int	read_char(EditLine *, Char *);
private int	read_getcmd(EditLine *, el_action_t *, Char *);
private void	read_pop(c_macro_t *);

/* read_init():
 *	Initialize the read stuff
 */
protected int
read_init(EditLine *el)
{
	/* builtin read_char */
	el->el_read.read_char = read_char;
	return 0;
}


/* el_read_setfn():
 *	Set the read char function to the one provided.
 *	If it is set to EL_BUILTIN_GETCFN, then reset to the builtin one.
 */
protected int
el_read_setfn(EditLine *el, el_rfunc_t rc)
{
	el->el_read.read_char = (rc == EL_BUILTIN_GETCFN) ? read_char : rc;
	return 0;
}


/* el_read_getfn():
 *	return the current read char function, or EL_BUILTIN_GETCFN
 *	if it is the default one
 */
protected el_rfunc_t
el_read_getfn(EditLine *el)
{
       return (el->el_read.read_char == read_char) ?
	    EL_BUILTIN_GETCFN : el->el_read.read_char;
}


#ifndef MIN
#define MIN(A,B) ((A) < (B) ? (A) : (B))
#endif

#ifdef DEBUG_EDIT
private void
read_debug(EditLine *el)
{

	if (el->el_line.cursor > el->el_line.lastchar)
		(void) fprintf(el->el_errfile, "cursor > lastchar\r\n");
	if (el->el_line.cursor < el->el_line.buffer)
		(void) fprintf(el->el_errfile, "cursor < buffer\r\n");
	if (el->el_line.cursor > el->el_line.limit)
		(void) fprintf(el->el_errfile, "cursor > limit\r\n");
	if (el->el_line.lastchar > el->el_line.limit)
		(void) fprintf(el->el_errfile, "lastchar > limit\r\n");
	if (el->el_line.limit != &el->el_line.buffer[EL_BUFSIZ - 2])
		(void) fprintf(el->el_errfile, "limit != &buffer[EL_BUFSIZ-2]\r\n");
}
#endif /* DEBUG_EDIT */


/* read__fixio():
 *	Try to recover from a read error
 */
/* ARGSUSED */
private int
read__fixio(int fd __attribute__((__unused__)), int e)
{

	switch (e) {
	case -1:		/* Make sure that the code is reachable */

#ifdef EWOULDBLOCK
	case EWOULDBLOCK:
#ifndef TRY_AGAIN
#define	TRY_AGAIN
#endif
#endif /* EWOULDBLOCK */

#if defined(POSIX) && defined(EAGAIN)
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
	case EAGAIN:
#ifndef TRY_AGAIN
#define	TRY_AGAIN
#endif
#endif /* EWOULDBLOCK && EWOULDBLOCK != EAGAIN */
#endif /* POSIX && EAGAIN */

		e = 0;
#ifdef TRY_AGAIN
#if defined(F_SETFL) && defined(O_NDELAY)
		if ((e = fcntl(fd, F_GETFL, 0)) == -1)
			return (-1);

		if (fcntl(fd, F_SETFL, e & ~O_NDELAY) == -1)
			return (-1);
		else
			e = 1;
#endif /* F_SETFL && O_NDELAY */

#ifdef FIONBIO
		{
			int zero = 0;

			if (ioctl(fd, FIONBIO, (ioctl_t) & zero) == -1)
				return (-1);
			else
				e = 1;
		}
#endif /* FIONBIO */

#endif /* TRY_AGAIN */
		return (e ? 0 : -1);

	case EINTR:
		return (0);

	default:
		return (-1);
	}
}


/* read_preread():
 *	Try to read the stuff in the input queue;
 */
private int
read_preread(EditLine *el)
{
	int chrs = 0;

	if (el->el_tty.t_mode == ED_IO)
		return (0);

#ifndef WIDECHAR
/* FIONREAD attempts to buffer up multiple bytes, and to make that work
 * properly with partial wide/UTF-8 characters would need some careful work. */
#ifdef FIONREAD
	(void) ioctl(el->el_infd, FIONREAD, (ioctl_t) & chrs);
	if (chrs > 0) {
		char buf[EL_BUFSIZ];

		chrs = read(el->el_infd, buf,
		    (size_t) MIN(chrs, EL_BUFSIZ - 1));
		if (chrs > 0) {
			buf[chrs] = '\0';
			el_push(el, buf);
		}
	}
#endif /* FIONREAD */
#endif
	return (chrs > 0);
}


/* el_push():
 *	Push a macro
 */
public void
FUN(el,push)(EditLine *el, const Char *str)
{
	c_macro_t *ma = &el->el_chared.c_macro;

	if (str != NULL && ma->level + 1 < EL_MAXMACRO) {
		ma->level++;
		if ((ma->macro[ma->level] = Strdup(str)) != NULL)
			return;
		ma->level--;
	}
	term_beep(el);
	term__flush(el);
}


/* read_getcmd():
 *	Return next command from the input stream.
 *	Character values > 255 are not looked up in the map, but inserted.
 */
private int
read_getcmd(EditLine *el, el_action_t *cmdnum, Char *ch)
{
	el_action_t cmd;
	int num;

	el->el_errno = 0;
	do {
		if ((num = FUN(el,getc)(el, ch)) != 1) {/* if EOF or error */
			el->el_errno = num == 0 ? 0 : errno;
			return (num);
		}

#ifdef	KANJI
		if ((*ch & 0200)) {
			el->el_state.metanext = 0;
			cmd = CcViMap[' '];
			break;
		} else
#endif /* KANJI */

		if (el->el_state.metanext) {
			el->el_state.metanext = 0;
			*ch |= 0200;
		}
#ifdef WIDECHAR
                if (*ch >= N_KEYS)
                        cmd = ED_INSERT;
		else
#endif
                        cmd = el->el_map.current[(unsigned char) *ch];
		if (cmd == ED_SEQUENCE_LEAD_IN) {
			key_value_t val;
			switch (key_get(el, ch, &val)) {
			case XK_CMD:
				cmd = val.cmd;
				break;
			case XK_STR:
				FUN(el,push)(el, val.str);
				break;
#ifdef notyet
			case XK_EXE:
				/* XXX: In the future to run a user function */
				RunCommand(val.str);
				break;
#endif
			default:
				EL_ABORT((el->el_errfile, "Bad XK_ type \n"));
				break;
			}
		}
		if (el->el_map.alt == NULL)
			el->el_map.current = el->el_map.key;
	} while (cmd == ED_SEQUENCE_LEAD_IN);
	*cmdnum = cmd;
	return (OKCMD);
}

#ifdef WIDECHAR
/* utf8_islead():
 *      Test whether a byte is a leading byte of a UTF-8 sequence.
 */
private int
utf8_islead(unsigned char c)
{
        return (c < 0x80) ||             /* single byte char */
               (c >= 0xc2 && c <= 0xf4); /* start of multibyte sequence */
}
#endif

/* read_char():
 *	Read a character from the tty.
 */
private int
read_char(EditLine *el, Char *cp)
{
	ssize_t num_read;
	int tried = 0;
        char cbuf[MB_LEN_MAX];
        int cbp = 0;
        int bytes = 0;

 again:
	el->el_signal->sig_no = 0;
	while ((num_read = read(el->el_infd, cbuf + cbp, 1)) == -1) {
		if (el->el_signal->sig_no == SIGCONT) {
			sig_set(el);
			el_set(el, EL_REFRESH);
			goto again;
		}
		if (!tried && read__fixio(el->el_infd, errno) == 0)
			tried = 1;
		else {
			*cp = '\0';
			return (-1);
		}
	}

#ifdef WIDECHAR
	if (el->el_flags & CHARSET_IS_UTF8) {
		if (!utf8_islead((unsigned char)cbuf[0]))
			goto again; /* discard the byte we read and try again */
		++cbp;
		if ((bytes = ct_mbtowc(cp, cbuf, cbp)) == -1) {
			ct_mbtowc_reset;
			if (cbp >= MB_LEN_MAX) { /* "shouldn't happen" */
				*cp = '\0';
				return (-1);
			}
			goto again;
		}
	} else  /* we don't support other multibyte charsets */
#endif
		*cp = (unsigned char)cbuf[0];

	if ((el->el_flags & IGNORE_EXTCHARS) && bytes > 1) {
		cbp = 0; /* skip this character */
		goto again;
	}

	return (int)num_read;
}

/* read_pop():
 *	Pop a macro from the stack
 */
private void
read_pop(c_macro_t *ma)
{
	int i;

	el_free(ma->macro[0]);
	for (i = 0; i < ma->level; i++)
		ma->macro[i] = ma->macro[i + 1];
	ma->level--;
	ma->offset = 0;
}

/* el_getc():
 *	Read a character
 */
public int
FUN(el,getc)(EditLine *el, Char *cp)
{
	int num_read;
	c_macro_t *ma = &el->el_chared.c_macro;

	term__flush(el);
	for (;;) {
		if (ma->level < 0) {
			if (!read_preread(el))
				break;
		}

		if (ma->level < 0)
			break;

		if (ma->macro[0][ma->offset] == '\0') {
			read_pop(ma);
			continue;
		}

		*cp = ma->macro[0][ma->offset++];

		if (ma->macro[0][ma->offset] == '\0') {
			/* Needed for QuoteMode On */
			read_pop(ma);
		}

		return (1);
	}

#ifdef DEBUG_READ
	(void) fprintf(el->el_errfile, "Turning raw mode on\n");
#endif /* DEBUG_READ */
	if (tty_rawmode(el) < 0)/* make sure the tty is set up correctly */
		return (0);

#ifdef DEBUG_READ
	(void) fprintf(el->el_errfile, "Reading a character\n");
#endif /* DEBUG_READ */
	num_read = (*el->el_read.read_char)(el, cp);
#ifdef WIDECHAR
	if (el->el_flags & NARROW_READ)
		*cp = *(char *)(void *)cp;
#endif
#ifdef DEBUG_READ
	(void) fprintf(el->el_errfile, "Got it %c\n", *cp);
#endif /* DEBUG_READ */
	return (num_read);
}

protected void
read_prepare(EditLine *el)
{
	if (el->el_flags & HANDLE_SIGNALS)
		sig_set(el);
	if (el->el_flags & NO_TTY)
		return;
	if ((el->el_flags & (UNBUFFERED|EDIT_DISABLED)) == UNBUFFERED)
		tty_rawmode(el);

	/* This is relatively cheap, and things go terribly wrong if
	   we have the wrong size. */
	el_resize(el);
	re_clear_display(el);	/* reset the display stuff */
	ch_reset(el, 0);
	re_refresh(el);		/* print the prompt */

	if (el->el_flags & UNBUFFERED)
		term__flush(el);
}

protected void
read_finish(EditLine *el)
{
	if ((el->el_flags & UNBUFFERED) == 0)
		(void) tty_cookedmode(el);
	if (el->el_flags & HANDLE_SIGNALS)
		sig_clr(el);
}

public const Char *
FUN(el,gets)(EditLine *el, int *nread)
{
	int retval;
	el_action_t cmdnum = 0;
	int num;		/* how many chars we have read at NL */
	Char ch;
	int crlf = 0;
	int nrb;
#ifdef FIONREAD
	c_macro_t *ma = &el->el_chared.c_macro;
#endif /* FIONREAD */

	if (nread == NULL)
		nread = &nrb;
	*nread = 0;

	if (el->el_flags & NO_TTY) {
		Char *cp = el->el_line.buffer;
		size_t idx;

		while ((num = (*el->el_read.read_char)(el, cp)) == 1) {
			/* make sure there is space for next character */
			if (cp + 1 >= el->el_line.limit) {
				idx = (cp - el->el_line.buffer);
				if (!ch_enlargebufs(el, 2))
					break;
				cp = &el->el_line.buffer[idx];
			}
			cp++;
			if (el->el_flags & UNBUFFERED)
				break;
			if (cp[-1] == '\r' || cp[-1] == '\n')
				break;
		}
		if (num == -1) {
			if (errno == EINTR)
				cp = el->el_line.buffer;
			el->el_errno = errno;
		}

		el->el_line.cursor = el->el_line.lastchar = cp;
		*cp = '\0';
		*nread = (int)(el->el_line.cursor - el->el_line.buffer);
		goto done;
	}


#ifdef FIONREAD
	if (el->el_tty.t_mode == EX_IO && ma->level < 0) {
		long chrs = 0;

		(void) ioctl(el->el_infd, FIONREAD, (ioctl_t) & chrs);
		if (chrs == 0) {
			if (tty_rawmode(el) < 0) {
				errno = 0;
				*nread = 0;
				return (NULL);
			}
		}
	}
#endif /* FIONREAD */

	if ((el->el_flags & UNBUFFERED) == 0)
		read_prepare(el);

	if (el->el_flags & EDIT_DISABLED) {
		Char *cp;
		size_t idx;

		if ((el->el_flags & UNBUFFERED) == 0)
			cp = el->el_line.buffer;
		else
			cp = el->el_line.lastchar;

		term__flush(el);

		while ((num = (*el->el_read.read_char)(el, cp)) == 1) {
			/* make sure there is space next character */
			if (cp + 1 >= el->el_line.limit) {
				idx = (cp - el->el_line.buffer);
				if (!ch_enlargebufs(el, 2))
					break;
				cp = &el->el_line.buffer[idx];
			}
			cp++;
			crlf = cp[-1] == '\r' || cp[-1] == '\n';
			if (el->el_flags & UNBUFFERED)
				break;
			if (crlf)
				break;
		}

		if (num == -1) {
			if (errno == EINTR)
				cp = el->el_line.buffer;
			el->el_errno = errno;
		}

		el->el_line.cursor = el->el_line.lastchar = cp;
		*cp = '\0';
		goto done;
	}

	for (num = OKCMD; num == OKCMD;) {	/* while still editing this
						 * line */
#ifdef DEBUG_EDIT
		read_debug(el);
#endif /* DEBUG_EDIT */
		/* if EOF or error */
		if ((num = read_getcmd(el, &cmdnum, &ch)) != OKCMD) {
#ifdef DEBUG_READ
			(void) fprintf(el->el_errfile,
			    "Returning from el_gets %d\n", num);
#endif /* DEBUG_READ */
			break;
		}
		if (el->el_errno == EINTR) {
			el->el_line.buffer[0] = '\0';
			el->el_line.lastchar =
			    el->el_line.cursor = el->el_line.buffer;
			break;
		}
		if ((unsigned int)cmdnum >= (unsigned int)el->el_map.nfunc) {	/* BUG CHECK command */
#ifdef DEBUG_EDIT
			(void) fprintf(el->el_errfile,
			    "ERROR: illegal command from key 0%o\r\n", ch);
#endif /* DEBUG_EDIT */
			continue;	/* try again */
		}
		/* now do the real command */
#ifdef DEBUG_READ
		{
			el_bindings_t *b;
			for (b = el->el_map.help; b->name; b++)
				if (b->func == cmdnum)
					break;
			if (b->name)
				(void) fprintf(el->el_errfile,
				    "Executing %s\n", b->name);
			else
				(void) fprintf(el->el_errfile,
				    "Error command = %d\n", cmdnum);
		}
#endif /* DEBUG_READ */
		/* vi redo needs these way down the levels... */
		el->el_state.thiscmd = cmdnum;
		el->el_state.thisch = ch;
		if (el->el_map.type == MAP_VI &&
		    el->el_map.current == el->el_map.key &&
		    el->el_chared.c_redo.pos < el->el_chared.c_redo.lim) {
			if (cmdnum == VI_DELETE_PREV_CHAR &&
			    el->el_chared.c_redo.pos != el->el_chared.c_redo.buf
			    && Isprint(el->el_chared.c_redo.pos[-1]))
				el->el_chared.c_redo.pos--;
			else
				*el->el_chared.c_redo.pos++ = ch;
		}
		retval = (*el->el_map.func[cmdnum]) (el, ch);
#ifdef DEBUG_READ
		(void) fprintf(el->el_errfile,
			"Returned state %d\n", retval );
#endif /* DEBUG_READ */

		/* save the last command here */
		el->el_state.lastcmd = cmdnum;

		/* use any return value */
		switch (retval) {
		case CC_CURSOR:
			re_refresh_cursor(el);
			break;

		case CC_REDISPLAY:
			re_clear_lines(el);
			re_clear_display(el);
			/* FALLTHROUGH */

		case CC_REFRESH:
			re_refresh(el);
			break;

		case CC_REFRESH_BEEP:
			re_refresh(el);
			term_beep(el);
			break;

		case CC_NORM:	/* normal char */
			break;

		case CC_ARGHACK:	/* Suggested by Rich Salz */
			/* <rsalz@pineapple.bbn.com> */
			continue;	/* keep going... */

		case CC_EOF:	/* end of file typed */
			if ((el->el_flags & UNBUFFERED) == 0)
				num = 0;
			else if (num == -1) {
				*el->el_line.lastchar++ = CONTROL('d');
				el->el_line.cursor = el->el_line.lastchar;
				num = 1;
			}
			break;

		case CC_NEWLINE:	/* normal end of line */
			num = (int)(el->el_line.lastchar - el->el_line.buffer);
			break;

		case CC_FATAL:	/* fatal error, reset to known state */
#ifdef DEBUG_READ
			(void) fprintf(el->el_errfile,
			    "*** editor fatal ERROR ***\r\n\n");
#endif /* DEBUG_READ */
			/* put (real) cursor in a known place */
			re_clear_display(el);	/* reset the display stuff */
			ch_reset(el, 1);	/* reset the input pointers */
			re_refresh(el);	/* print the prompt again */
			break;

		case CC_ERROR:
		default:	/* functions we don't know about */
#ifdef DEBUG_READ
			(void) fprintf(el->el_errfile,
			    "*** editor ERROR ***\r\n\n");
#endif /* DEBUG_READ */
			term_beep(el);
			term__flush(el);
			break;
		}
		el->el_state.argument = 1;
		el->el_state.doingarg = 0;
		el->el_chared.c_vcmd.action = NOP;
		if (el->el_flags & UNBUFFERED)
			break;
	}

	term__flush(el);		/* flush any buffered output */
	/* make sure the tty is set up correctly */
	if ((el->el_flags & UNBUFFERED) == 0) {
		read_finish(el);
		*nread = num != -1 ? num : 0;
	} else {
		*nread = (int)(el->el_line.lastchar - el->el_line.buffer);
	}
done:
	if (*nread == 0) {
		if (num == -1) {
			*nread = -1;
			errno = el->el_errno;
		}
		return NULL;
	} else
		return el->el_line.buffer;
}