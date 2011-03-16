/*	$OpenBSD: tree.c,v 1.19 2008/08/11 21:50:35 jaredy Exp $	*/

/*-
 * Copyright (c) 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011
 *	Thorsten Glaser <tg@mirbsd.org>
 *
 * Provided that these terms and disclaimer and all copyright notices
 * are retained or reproduced in an accompanying document, permission
 * is granted to deal in this work without restriction, including un-
 * limited rights to use, publicly perform, distribute, sell, modify,
 * merge, give away, or sublicence.
 *
 * This work is provided "AS IS" and WITHOUT WARRANTY of any kind, to
 * the utmost extent permitted by applicable law, neither express nor
 * implied; without malicious intent or gross negligence. In no event
 * may a licensor, author or contributor be held liable for indirect,
 * direct, other damage, loss, or other issues arising in any way out
 * of dealing in the work, even if advised of the possibility of such
 * damage or existence of a defect, except proven that it results out
 * of said person's immediate fault when using the work as intended.
 */

#include "sh.h"

__RCSID("$MirOS: src/bin/mksh/tree.c,v 1.40 2011/03/13 01:20:24 tg Exp $");

#define INDENT	8

static void ptree(struct op *, int, struct shf *);
static void pioact(struct shf *, int, struct ioword *);
static void tputS(const char *, struct shf *);
static void vfptreef(struct shf *, int, const char *, va_list);
static struct ioword **iocopy(struct ioword **, Area *);
static void iofree(struct ioword **, Area *);
static void wdstrip_internal(struct shf *, const char *, bool, bool);

/* "foo& ; bar" and "foo |& ; bar" are invalid */
static bool prevent_semicolon = false;

/*
 * print a command tree
 */
static void
ptree(struct op *t, int indent, struct shf *shf)
{
	const char **w;
	struct ioword **ioact;
	struct op *t1;
	int i;

 Chain:
	if (t == NULL)
		return;
	switch (t->type) {
	case TCOM:
		if (t->vars) {
			w = (const char **)t->vars;
			while (*w)
				fptreef(shf, indent, "%S ", *w++);
		} else
			shf_puts("#no-vars# ", shf);
		if (t->args) {
			w = t->args;
			while (*w)
				fptreef(shf, indent, "%S ", *w++);
		} else
			shf_puts("#no-args# ", shf);
		prevent_semicolon = false;
		break;
	case TEXEC:
		t = t->left;
		goto Chain;
	case TPAREN:
		fptreef(shf, indent + 2, "( %T) ", t->left);
		break;
	case TPIPE:
		fptreef(shf, indent, "%T| ", t->left);
		t = t->right;
		goto Chain;
	case TLIST:
		fptreef(shf, indent, "%T%;", t->left);
		t = t->right;
		goto Chain;
	case TOR:
	case TAND:
		fptreef(shf, indent, "%T%s %T",
		    t->left, (t->type == TOR) ? "||" : "&&", t->right);
		break;
	case TBANG:
		shf_puts("! ", shf);
		prevent_semicolon = false;
		t = t->right;
		goto Chain;
	case TDBRACKET:
		w = t->args;
		shf_puts("[[", shf);
		while (*w)
			fptreef(shf, indent, " %S", *w++);
		shf_puts(" ]] ", shf);
		break;
	case TSELECT:
	case TFOR:
		fptreef(shf, indent, "%s %s ",
		    (t->type == TFOR) ? "for" : "select", t->str);
		if (t->vars != NULL) {
			shf_puts("in ", shf);
			w = (const char **)t->vars;
			while (*w)
				fptreef(shf, indent, "%S ", *w++);
			fptreef(shf, indent, "%;");
		}
		fptreef(shf, indent + INDENT, "do%N%T", t->left);
		fptreef(shf, indent, "%;done ");
		break;
	case TCASE:
		fptreef(shf, indent, "case %S in", t->str);
		for (t1 = t->left; t1 != NULL; t1 = t1->right) {
			fptreef(shf, indent, "%N(");
			w = (const char **)t1->vars;
			while (*w) {
				fptreef(shf, indent, "%S%c", *w,
				    (w[1] != NULL) ? '|' : ')');
				++w;
			}
			fptreef(shf, indent + INDENT, "%N%T%N;;", t1->left);
		}
		fptreef(shf, indent, "%Nesac ");
		break;
#ifndef MKSH_NO_DEPRECATED_WARNING
	case TELIF:
		internal_errorf("TELIF in tree.c:ptree() unexpected");
		/* FALLTHROUGH */
#endif
	case TIF:
		i = 2;
		goto process_TIF;
		do {
			t = t->right;
			i = 0;
			fptreef(shf, indent, "%;");
 process_TIF:
			/* 5 == strlen("elif ") */
			fptreef(shf, indent + 5 - i, "elif %T" + i, t->left);
			t = t->right;
			if (t->left != NULL) {
				fptreef(shf, indent, "%;");
				fptreef(shf, indent + INDENT, "%s%N%T",
				    "then", t->left);
			}
		} while (t->right && t->right->type == TELIF);
		if (t->right != NULL) {
			fptreef(shf, indent, "%;");
			fptreef(shf, indent + INDENT, "%s%N%T",
			    "else", t->right);
		}
		fptreef(shf, indent, "%;fi ");
		break;
	case TWHILE:
	case TUNTIL:
		/* 6 == strlen("while"/"until") */
		fptreef(shf, indent + 6, "%s %T",
		    (t->type == TWHILE) ? "while" : "until",
		    t->left);
		fptreef(shf, indent, "%;");
		fptreef(shf, indent + INDENT, "do%N%T", t->right);
		fptreef(shf, indent, "%;done ");
		break;
	case TBRACE:
		fptreef(shf, indent + INDENT, "{%N%T", t->left);
		fptreef(shf, indent, "%;} ");
		break;
	case TCOPROC:
		fptreef(shf, indent, "%T|& ", t->left);
		prevent_semicolon = true;
		break;
	case TASYNC:
		fptreef(shf, indent, "%T& ", t->left);
		prevent_semicolon = true;
		break;
	case TFUNCT:
		fpFUNCTf(shf, indent, t->u.ksh_func, t->str, t->left);
		break;
	case TTIME:
		fptreef(shf, indent, "%s %T", "time", t->left);
		break;
	default:
		shf_puts("<botch>", shf);
		prevent_semicolon = false;
		break;
	}
	if ((ioact = t->ioact) != NULL) {
		bool need_nl = false;

		while (*ioact != NULL)
			pioact(shf, indent, *ioact++);
		/* Print here documents after everything else... */
		ioact = t->ioact;
		while (*ioact != NULL) {
			struct ioword *iop = *ioact++;

			/* heredoc is 0 when tracing (set -x) */
			if ((iop->flag & IOTYPE) == IOHERE && iop->heredoc &&
			    /* iop->delim[1] == '<' means here string */
			    (!iop->delim || iop->delim[1] != '<')) {
				shf_putc('\n', shf);
				shf_puts(iop->heredoc, shf);
				fptreef(shf, indent, "%s",
				    evalstr(iop->delim, 0));
				need_nl = true;
			}
		}
		/*
		 * Last delimiter must be followed by a newline (this
		 * often leads to an extra blank line, but it's not
		 * worth worrying about)
		 */
		if (need_nl)
			shf_putc('\n', shf);
	}
}

static void
pioact(struct shf *shf, int indent, struct ioword *iop)
{
	int flag = iop->flag;
	int type = flag & IOTYPE;
	int expected;

	expected = (type == IOREAD || type == IORDWR || type == IOHERE) ? 0 :
	    (type == IOCAT || type == IOWRITE) ? 1 :
	    (type == IODUP && (iop->unit == !(flag & IORDUP))) ? iop->unit :
	    iop->unit + 1;
	if (iop->unit != expected)
		shf_fprintf(shf, "%d", iop->unit);

	switch (type) {
	case IOREAD:
		shf_puts("<", shf);
		break;
	case IOHERE:
		shf_puts(flag & IOSKIP ? "<<-" : "<<", shf);
		break;
	case IOCAT:
		shf_puts(">>", shf);
		break;
	case IOWRITE:
		shf_puts(flag & IOCLOB ? ">|" : ">", shf);
		break;
	case IORDWR:
		shf_puts("<>", shf);
		break;
	case IODUP:
		shf_puts(flag & IORDUP ? "<&" : ">&", shf);
		break;
	}
	/* name/delim are NULL when printing syntax errors */
	if (type == IOHERE) {
		if (iop->delim)
			fptreef(shf, indent, "%S ", iop->delim);
		else
			shf_putc(' ', shf);
	} else if (iop->name)
		fptreef(shf, indent, (iop->flag & IONAMEXP) ? "%s " : "%S ",
		    iop->name);
	prevent_semicolon = false;
}

/* variant of fputs for ptreef */
static void
tputS(const char *wp, struct shf *shf)
{
	int c, quotelevel = 0;

	/*-
	 * problems:
	 *	`...` -> $(...)
	 *	'foo' -> "foo"
	 * could change encoding to:
	 *	OQUOTE ["'] ... CQUOTE ["']
	 *	COMSUB [(`] ...\0	(handle $ ` \ and maybe " in `...` case)
	 */
	while (/* CONSTCOND */ 1)
		switch (*wp++) {
		case EOS:
			return;
		case ADELIM:
		case CHAR:
			shf_putchar(*wp++, shf);
			break;
		case QCHAR:
			c = *wp++;
			if (!quotelevel ||
			    (c == '"' || c == '`' || c == '$' || c == '\\'))
				shf_putc('\\', shf);
			shf_putc(c, shf);
			break;
		case COMSUB:
			shf_puts("$(", shf);
			while ((c = *wp++) != 0)
				shf_putc(c, shf);
			shf_putc(')', shf);
			break;
		case EXPRSUB:
			shf_puts("$((", shf);
			while ((c = *wp++) != 0)
				shf_putc(c, shf);
			shf_puts("))", shf);
			break;
		case OQUOTE:
			quotelevel++;
			shf_putc('"', shf);
			break;
		case CQUOTE:
			if (quotelevel)
				quotelevel--;
			shf_putc('"', shf);
			break;
		case OSUBST:
			shf_putc('$', shf);
			if (*wp++ == '{')
				shf_putc('{', shf);
			while ((c = *wp++) != 0)
				shf_putc(c, shf);
			break;
		case CSUBST:
			if (*wp++ == '}')
				shf_putc('}', shf);
			break;
		case OPAT:
			shf_putchar(*wp++, shf);
			shf_putc('(', shf);
			break;
		case SPAT:
			shf_putc('|', shf);
			break;
		case CPAT:
			shf_putc(')', shf);
			break;
		}
}

/*
 * this is the _only_ way to reliably handle
 * variable args with an ANSI compiler
 */
/* VARARGS */
void
fptreef(struct shf *shf, int indent, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	vfptreef(shf, indent, fmt, va);
	va_end(va);
}

/* VARARGS */
char *
snptreef(char *s, int n, const char *fmt, ...)
{
	va_list va;
	struct shf shf;

	shf_sopen(s, n, SHF_WR | (s ? 0 : SHF_DYNAMIC), &shf);

	va_start(va, fmt);
	vfptreef(&shf, 0, fmt, va);
	va_end(va);

	/* shf_sclose NUL terminates */
	return (shf_sclose(&shf));
}

static void
vfptreef(struct shf *shf, int indent, const char *fmt, va_list va)
{
	int c;

	while ((c = *fmt++)) {
		if (c == '%') {
			switch ((c = *fmt++)) {
			case 'c':
				/* character (octet, probably) */
				shf_putchar(va_arg(va, int), shf);
				break;
			case 's':
				/* string */
				shf_puts(va_arg(va, char *), shf);
				break;
			case 'S':
				/* word */
				tputS(va_arg(va, char *), shf);
				break;
			case 'd':
				/* signed decimal */
				shf_fprintf(shf, "%d", va_arg(va, int));
				break;
			case 'u':
				/* unsigned decimal */
				shf_fprintf(shf, "%u", va_arg(va, unsigned int));
				break;
			case 'T':
				/* format tree */
				ptree(va_arg(va, struct op *), indent, shf);
				goto dont_trash_prevent_semicolon;
			case ';':
				/* newline or ; */
			case 'N':
				/* newline or space */
				if (shf->flags & SHF_STRING) {
					if (c == ';' && !prevent_semicolon)
						shf_putc(';', shf);
					shf_putc(' ', shf);
				} else {
					int i;

					shf_putc('\n', shf);
					i = indent;
					while (i >= 8) {
						shf_putc('\t', shf);
						i -= 8;
					}
					while (i--)
						shf_putc(' ', shf);
				}
				break;
			case 'R':
				/* I/O redirection */
				pioact(shf, indent, va_arg(va, struct ioword *));
				break;
			default:
				shf_putc(c, shf);
				break;
			}
		} else
			shf_putc(c, shf);
		prevent_semicolon = false;
 dont_trash_prevent_semicolon:
		;
	}
}

/*
 * copy tree (for function definition)
 */
struct op *
tcopy(struct op *t, Area *ap)
{
	struct op *r;
	const char **tw;
	char **rw;

	if (t == NULL)
		return (NULL);

	r = alloc(sizeof(struct op), ap);

	r->type = t->type;
	r->u.evalflags = t->u.evalflags;

	if (t->type == TCASE)
		r->str = wdcopy(t->str, ap);
	else
		strdupx(r->str, t->str, ap);

	if (t->vars == NULL)
		r->vars = NULL;
	else {
		tw = (const char **)t->vars;
		while (*tw)
			++tw;
		rw = r->vars = alloc2(tw - (const char **)t->vars + 1,
		    sizeof(*tw), ap);
		tw = (const char **)t->vars;
		while (*tw)
			*rw++ = wdcopy(*tw++, ap);
		*rw = NULL;
	}

	if (t->args == NULL)
		r->args = NULL;
	else {
		tw = t->args;
		while (*tw)
			++tw;
		r->args = (const char **)(rw = alloc2(tw - t->args + 1,
		    sizeof(*tw), ap));
		tw = t->args;
		while (*tw)
			*rw++ = wdcopy(*tw++, ap);
		*rw = NULL;
	}

	r->ioact = (t->ioact == NULL) ? NULL : iocopy(t->ioact, ap);

	r->left = tcopy(t->left, ap);
	r->right = tcopy(t->right, ap);
	r->lineno = t->lineno;

	return (r);
}

char *
wdcopy(const char *wp, Area *ap)
{
	size_t len;

	len = wdscan(wp, EOS) - wp;
	return (memcpy(alloc(len, ap), wp, len));
}

/* return the position of prefix c in wp plus 1 */
const char *
wdscan(const char *wp, int c)
{
	int nest = 0;

	while (/* CONSTCOND */ 1)
		switch (*wp++) {
		case EOS:
			return (wp);
		case ADELIM:
			if (c == ADELIM)
				return (wp + 1);
			/* FALLTHROUGH */
		case CHAR:
		case QCHAR:
			wp++;
			break;
		case COMSUB:
		case EXPRSUB:
			while (*wp++ != 0)
				;
			break;
		case OQUOTE:
		case CQUOTE:
			break;
		case OSUBST:
			nest++;
			while (*wp++ != '\0')
				;
			break;
		case CSUBST:
			wp++;
			if (c == CSUBST && nest == 0)
				return (wp);
			nest--;
			break;
		case OPAT:
			nest++;
			wp++;
			break;
		case SPAT:
		case CPAT:
			if (c == wp[-1] && nest == 0)
				return (wp);
			if (wp[-1] == CPAT)
				nest--;
			break;
		default:
			internal_warningf(
			    "wdscan: unknown char 0x%x (carrying on)",
			    wp[-1]);
		}
}

/*
 * return a copy of wp without any of the mark up characters and with
 * quote characters (" ' \) stripped. (string is allocated from ATEMP)
 */
char *
wdstrip(const char *wp, bool keepq, bool make_magic)
{
	struct shf shf;

	shf_sopen(NULL, 32, SHF_WR | SHF_DYNAMIC, &shf);
	wdstrip_internal(&shf, wp, keepq, make_magic);
	/* shf_sclose NUL terminates */
	return (shf_sclose(&shf));
}

static void
wdstrip_internal(struct shf *shf, const char *wp, bool keepq, bool make_magic)
{
	int c;

	/*-
	 * problems:
	 *	`...` -> $(...)
	 *	x${foo:-"hi"} -> x${foo:-hi}
	 *	x${foo:-'hi'} -> x${foo:-hi} unless keepq
	 */
	while (/* CONSTCOND */ 1)
		switch (*wp++) {
		case EOS:
			return;
		case ADELIM:
		case CHAR:
			c = *wp++;
			if (make_magic && (ISMAGIC(c) || c == '[' || c == NOT ||
			    c == '-' || c == ']' || c == '*' || c == '?'))
				shf_putc(MAGIC, shf);
			shf_putc(c, shf);
			break;
		case QCHAR:
			c = *wp++;
			if (keepq && (c == '"' || c == '`' || c == '$' || c == '\\'))
				shf_putc('\\', shf);
			shf_putc(c, shf);
			break;
		case COMSUB:
			shf_puts("$(", shf);
			while ((c = *wp++) != 0)
				shf_putc(c, shf);
			shf_putc(')', shf);
			break;
		case EXPRSUB:
			shf_puts("$((", shf);
			while (*wp != 0)
				shf_putchar(*wp++, shf);
			shf_puts("))", shf);
			break;
		case OQUOTE:
			break;
		case CQUOTE:
			break;
		case OSUBST:
			shf_putc('$', shf);
			if (*wp++ == '{')
			    shf_putc('{', shf);
			while ((c = *wp++) != 0)
				shf_putc(c, shf);
			break;
		case CSUBST:
			if (*wp++ == '}')
				shf_putc('}', shf);
			break;
		case OPAT:
			if (make_magic) {
				shf_putc(MAGIC, shf);
				shf_putchar(*wp++ | 0x80, shf);
			} else {
				shf_putchar(*wp++, shf);
				shf_putc('(', shf);
			}
			break;
		case SPAT:
			if (make_magic)
				shf_putc(MAGIC, shf);
			shf_putc('|', shf);
			break;
		case CPAT:
			if (make_magic)
				shf_putc(MAGIC, shf);
			shf_putc(')', shf);
			break;
		}
}

static struct ioword **
iocopy(struct ioword **iow, Area *ap)
{
	struct ioword **ior;
	int i;

	ior = iow;
	while (*ior)
		++ior;
	ior = alloc2(ior - iow + 1, sizeof(struct ioword *), ap);

	for (i = 0; iow[i] != NULL; i++) {
		struct ioword *p, *q;

		p = iow[i];
		q = alloc(sizeof(struct ioword), ap);
		ior[i] = q;
		*q = *p;
		if (p->name != NULL)
			q->name = wdcopy(p->name, ap);
		if (p->delim != NULL)
			q->delim = wdcopy(p->delim, ap);
		if (p->heredoc != NULL)
			strdupx(q->heredoc, p->heredoc, ap);
	}
	ior[i] = NULL;

	return (ior);
}

/*
 * free tree (for function definition)
 */
void
tfree(struct op *t, Area *ap)
{
	char **w;

	if (t == NULL)
		return;

	if (t->str != NULL)
		afree(t->str, ap);

	if (t->vars != NULL) {
		for (w = t->vars; *w != NULL; w++)
			afree(*w, ap);
		afree(t->vars, ap);
	}

	if (t->args != NULL) {
		/*XXX we assume the caller is right */
		union mksh_ccphack cw;

		cw.ro = t->args;
		for (w = cw.rw; *w != NULL; w++)
			afree(*w, ap);
		afree(t->args, ap);
	}

	if (t->ioact != NULL)
		iofree(t->ioact, ap);

	tfree(t->left, ap);
	tfree(t->right, ap);

	afree(t, ap);
}

static void
iofree(struct ioword **iow, Area *ap)
{
	struct ioword **iop;
	struct ioword *p;

	iop = iow;
	while ((p = *iop++) != NULL) {
		if (p->name != NULL)
			afree(p->name, ap);
		if (p->delim != NULL)
			afree(p->delim, ap);
		if (p->heredoc != NULL)
			afree(p->heredoc, ap);
		afree(p, ap);
	}
	afree(iow, ap);
}

void
fpFUNCTf(struct shf *shf, int i, bool isksh, const char *k, struct op *v)
{
	if (isksh)
		fptreef(shf, i, "%s %s %T", T_function, k, v);
	else
		fptreef(shf, i, "%s() %T", k, v);
}


/* for jobs.c */
void
vistree(char *dst, size_t sz, struct op *t)
{
	int c;
	char *cp, *buf;

	buf = alloc(sz, ATEMP);
	snptreef(buf, sz, "%T", t);
	cp = buf;
	while ((c = *cp++)) {
		if (((c & 0x60) == 0) || ((c & 0x7F) == 0x7F)) {
			/* C0 or C1 control character or DEL */
			if (!--sz)
				break;
			*dst++ = (c & 0x80) ? '$' : '^';
			c = (c & 0x7F) ^ 0x40;
		}
		if (!--sz)
			break;
		*dst++ = c;
	}
	*dst = '\0';
	afree(buf, ATEMP);
}

#ifdef DEBUG
static void
dumpchar(struct shf *shf, int c)
{
	if (((c & 0x60) == 0) || ((c & 0x7F) == 0x7F)) {
		/* C0 or C1 control character or DEL */
		shf_putc((c & 0x80) ? '$' : '^', shf);
		c = (c & 0x7F) ^ 0x40;
	}
	shf_putc(c, shf);
}

/* see: tputS */
void
dumpwdvar(struct shf *shf, const char *wp)
{
	int c, quotelevel = 0;

	while (/* CONSTCOND */ 1) {
		switch(*wp++) {
		case EOS:
			shf_puts("EOS", shf);
			return;
		case ADELIM:
			shf_puts("ADELIM=", shf);
 dumpchar:
			dumpchar(shf, *wp++);
			break;
		case CHAR:
			shf_puts("CHAR=", shf);
			goto dumpchar;
		case QCHAR:
			shf_puts("QCHAR<", shf);
			c = *wp++;
			if (!quotelevel ||
			    (c == '"' || c == '`' || c == '$' || c == '\\'))
				shf_putc('\\', shf);
			dumpchar(shf, c);
			goto closeandout;
		case COMSUB:
			shf_puts("COMSUB<", shf);
 dumpsub:
			while ((c = *wp++) != 0)
				dumpchar(shf, c);
 closeandout:
			shf_putc('>', shf);
			break;
		case EXPRSUB:
			shf_puts("EXPRSUB<", shf);
			goto dumpsub;
		case OQUOTE:
			shf_fprintf(shf, "OQUOTE{%d", ++quotelevel);
			break;
		case CQUOTE:
			shf_fprintf(shf, "%d}CQUOTE", quotelevel);
			if (quotelevel)
				quotelevel--;
			else
				shf_puts("(err)", shf);
			break;
		case OSUBST:
			shf_puts("OSUBST(", shf);
			dumpchar(shf, *wp++);
			shf_puts(")[", shf);
			while ((c = *wp++) != 0)
				dumpchar(shf, c);
			break;
		case CSUBST:
			shf_puts("]CSUBST(", shf);
			dumpchar(shf, *wp++);
			shf_putc(')', shf);
			break;
		case OPAT:
			shf_puts("OPAT=", shf);
			dumpchar(shf, *wp++);
			break;
		case SPAT:
			shf_puts("SPAT", shf);
			break;
		case CPAT:
			shf_puts("CPAT", shf);
			break;
		default:
			shf_fprintf(shf, "INVAL<%u>", (uint8_t)wp[-1]);
			break;
		}
		shf_putc(' ', shf);
	}
}

void
dumptree(struct shf *shf, struct op *t)
{
	int i;
	const char **w, *name;
	struct op *t1;
	static int nesting = 0;

	for (i = 0; i < nesting; ++i)
		shf_putc('\t', shf);
	++nesting;
	shf_puts("{tree:" /*}*/, shf);
	if (t == NULL) {
		name = "(null)";
		goto out;
	}
	switch (t->type) {
#define OPEN(x) case x: name = #x; shf_puts(" {" #x ":", shf); /*}*/

	OPEN(TCOM)
		if (t->vars) {
			i = 0;
			w = (const char **)t->vars;
			while (*w) {
				shf_putc('\n', shf);
				for (int j = 0; j < nesting; ++j)
					shf_putc('\t', shf);
				shf_fprintf(shf, " var%d<", i++);
				dumpwdvar(shf, *w++);
				shf_putc('>', shf);
			}
		} else
			shf_puts(" #no-vars#", shf);
		if (t->args) {
			i = 0;
			w = t->args;
			while (*w) {
				shf_putc('\n', shf);
				for (int j = 0; j < nesting; ++j)
					shf_putc('\t', shf);
				shf_fprintf(shf, " arg%d<", i++);
				dumpwdvar(shf, *w++);
				shf_putc('>', shf);
			}
		} else
			shf_puts(" #no-args#", shf);
		break;
	OPEN(TEXEC)
 dumpleftandout:
		t = t->left;
 dumpandout:
		shf_putc('\n', shf);
		dumptree(shf, t);
		break;
	OPEN(TPAREN)
		goto dumpleftandout;
	OPEN(TPIPE)
 dumpleftmidrightandout:
		shf_putc('\n', shf);
		dumptree(shf, t->left);
// middumprightandout:
		shf_fprintf(shf, "/%s:", name);
 dumprightandout:
		t = t->right;
		goto dumpandout;
	OPEN(TLIST)
		goto dumpleftmidrightandout;
	OPEN(TOR)
		goto dumpleftmidrightandout;
	OPEN(TAND)
		goto dumpleftmidrightandout;
	OPEN(TBANG)
		goto dumprightandout;
	OPEN(TDBRACKET)
		i = 0;
		w = t->args;
		while (*w) {
			shf_putc('\n', shf);
			for (int j = 0; j < nesting; ++j)
				shf_putc('\t', shf);
			shf_fprintf(shf, " arg%d<", i++);
			dumpwdvar(shf, *w++);
			shf_putc('>', shf);
		}
		break;
	OPEN(TFOR)
 dumpfor:
		shf_fprintf(shf, " str<%s>", t->str);
		if (t->vars != NULL) {
			i = 0;
			w = (const char **)t->vars;
			while (*w) {
				shf_putc('\n', shf);
				for (int j = 0; j < nesting; ++j)
					shf_putc('\t', shf);
				shf_fprintf(shf, " var%d<", i++);
				dumpwdvar(shf, *w++);
				shf_putc('>', shf);
			}
		}
		goto dumpleftandout;
	OPEN(TSELECT)
		goto dumpfor;
	OPEN(TCASE)
		shf_fprintf(shf, " str<%s>", t->str);
		i = 0;
		for (t1 = t->left; t1 != NULL; t1 = t1->right) {
			shf_putc('\n', shf);
			for (int j = 0; j < nesting; ++j)
				shf_putc('\t', shf);
			shf_fprintf(shf, " sub%d[(", i);
			w = (const char **)t1->vars;
			while (*w) {
				dumpwdvar(shf, *w);
				if (w[1] != NULL)
					shf_putc('|', shf);
				++w;
			}
			shf_putc(')', shf);
			shf_putc('\n', shf);
			dumptree(shf, t1->left);
			shf_fprintf(shf, " /%d]", i++);
		}
		break;
	OPEN(TWHILE)
		goto dumpleftmidrightandout;
	OPEN(TUNTIL)
		goto dumpleftmidrightandout;
	OPEN(TBRACE)
		goto dumpleftandout;
	OPEN(TCOPROC)
		goto dumpleftandout;
	OPEN(TASYNC)
		goto dumpleftandout;
	OPEN(TFUNCT)
		shf_fprintf(shf, " str<%s> ksh<%s>", t->str,
		    t->u.ksh_func ? "yes" : "no");
		goto dumpleftandout;
	OPEN(TTIME)
		goto dumpleftandout;
	OPEN(TIF)
 dumpif:
		shf_putc('\n', shf);
		dumptree(shf, t->left);
		t = t->right;
		if (t->left != NULL) {
			shf_puts(" /TTHEN:\n", shf);
			dumptree(shf, t->left);
		}
		if (t->right && t->right->type == TELIF) {
			shf_puts(" /TELIF:", shf);
			t = t->right;
			goto dumpif;
		}
		if (t->right != NULL) {
			shf_puts(" /TELSE:\n", shf);
			dumptree(shf, t->right);
		}
		break;
	OPEN(TEOF)
 dumpunexpected:
		shf_puts("unexpected", shf);
		break;
	OPEN(TELIF)
		goto dumpunexpected;
	OPEN(TPAT)
		goto dumpunexpected;
	default:
		name = "TINVALID";
		shf_fprintf(shf, "{T<%d>:" /*}*/, t->type);
		goto dumpunexpected;

#undef OPEN
	}
 out:
	shf_fprintf(shf, /*{*/ " /%s}\n", name);
	--nesting;
}
#endif
