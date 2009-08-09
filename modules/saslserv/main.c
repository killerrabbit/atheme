/*
 * Copyright (c) 2005 Atheme Development Group
 * Rights to this code are documented in doc/LICENSE.
 *
 * This file contains the main() routine.
 *
 * $Id: main.c 8375 2007-06-03 20:03:26Z pippijn $
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"saslserv/main", false, _modinit, _moddeinit,
	"$Id: main.c 8375 2007-06-03 20:03:26Z pippijn $",
	"Atheme Development Group <http://www.atheme.org>"
);

list_t saslserv_conftable;

list_t sessions;
list_t sasl_mechanisms;

sasl_session_t *find_session(char *uid);
sasl_session_t *make_session(char *uid);
void destroy_session(sasl_session_t *p);
static void sasl_logcommand(sasl_session_t *p, myuser_t *login, int level, const char *fmt, ...);
static void sasl_input(sasl_message_t *smsg);
static void sasl_packet(sasl_session_t *p, char *buf, int len);
static void sasl_write(char *target, char *data, int length);
int login_user(sasl_session_t *p);
static void sasl_newuser(user_t *u);
static void delete_stale(void *vptr);

/* main services client routine */
static void saslserv(sourceinfo_t *si, int parc, char *parv[])
{
	char *cmd;
	char *text;
	char orig[BUFSIZE];
	
	/* this should never happen */
	if (parv[0][0] == '&')
	{
		slog(LG_ERROR, "services(): got parv with local channel: %s", parv[0]);
		return;
	}
	
	/* make a copy of the original for debugging */
	strlcpy(orig, parv[parc - 1], BUFSIZE);
	
	/* lets go through this to get the command */
	cmd = strtok(parv[parc - 1], " ");
	text = strtok(NULL, "");
	
	if (!cmd)
		return;
	if (*cmd == '\001')
	{
		handle_ctcp_common(si, cmd, text);
		return;
	}
	
	command_fail(si, fault_noprivs, "This service exists to identify "
			"connecting clients to the network. It has no "
			"public interface.");
}

static void saslserv_config_ready(void *unused)
{
	saslsvs.nick = saslsvs.me->nick;
	saslsvs.user = saslsvs.me->user;
	saslsvs.host = saslsvs.me->host;
	saslsvs.real = saslsvs.me->real;
	saslsvs.disp = saslsvs.me->disp;
}

void _modinit(module_t *m)
{
	hook_add_event("config_ready");
	hook_add_config_ready(saslserv_config_ready);
	hook_add_event("sasl_input");
	hook_add_sasl_input(sasl_input);
	hook_add_event("user_add");
	hook_add_user_add(sasl_newuser);
	event_add("sasl_delete_stale", delete_stale, NULL, 30);

	saslsvs.me = service_add("saslserv", saslserv, NULL, &saslserv_conftable);
	authservice_loaded++;
}

void _moddeinit(void)
{
	node_t *n, *tn;

	hook_del_sasl_input(sasl_input);
	hook_del_user_add(sasl_newuser);
	event_delete(delete_stale, NULL);

        if (saslsvs.me)
	{
		saslsvs.nick = NULL;
		saslsvs.user = NULL;
		saslsvs.host = NULL;
		saslsvs.real = NULL;
		saslsvs.disp = NULL;
                service_delete(saslsvs.me);
		saslsvs.me = NULL;
	}
	authservice_loaded--;

	LIST_FOREACH_SAFE(n, tn, sessions.head)
	{
		destroy_session(n->data);
		node_del(n, &sessions);
		node_free(n);
	}
}

/*
 * Begin SASL-specific code
 */

/* find an existing session by uid */
sasl_session_t *find_session(char *uid)
{
	sasl_session_t *p;
	node_t *n;

	LIST_FOREACH(n, sessions.head)
	{
		p = n->data;
		if(!strcmp(p->uid, uid))
			return p;
	}

	return NULL;
}

/* create a new session if it does not already exist */
sasl_session_t *make_session(char *uid)
{
	sasl_session_t *p = find_session(uid);
	node_t *n;

	if(p)
		return p;

	p = malloc(sizeof(sasl_session_t));
	memset(p, 0, sizeof(sasl_session_t));
	strlcpy(p->uid, uid, IDLEN);

	n = node_create();
	node_add(p, n, &sessions);

	return p;
}

/* free a session and all its contents */
void destroy_session(sasl_session_t *p)
{
	node_t *n, *tn;
	myuser_t *mu;

	if (p->flags & ASASL_NEED_LOG && p->username != NULL)
	{
		mu = myuser_find(p->username);
		if (mu != NULL)
			sasl_logcommand(p, mu, CMDLOG_LOGIN, "LOGIN (session timed out)");
	}

	LIST_FOREACH_SAFE(n, tn, sessions.head)
	{
		if(n->data == p)
		{
			node_del(n, &sessions);
			node_free(n);
		}
	}

	free(p->buf);
	p->buf = p->p = NULL;
	if(p->mechptr)
		p->mechptr->mech_finish(p); /* Free up any mechanism data */
	p->mechptr = NULL; /* We're not freeing the mechanism, just "dereferencing" it */
	free(p->username);

	free(p);
}

/* interpret an AUTHENTICATE message */
static void sasl_input(sasl_message_t *smsg)
{
	sasl_session_t *p = make_session(smsg->uid);
	int len = strlen(smsg->buf);

	/* Abort packets, or maybe some other kind of (D)one */
	if(smsg->mode == 'D')
	{
		destroy_session(p);
		return;
	}

	if(smsg->mode != 'S' && smsg->mode != 'C')
		return;

	if(p->buf == NULL)
	{
		p->buf = (char *)malloc(len + 1);
		p->p = p->buf;
		p->len = len;
	}
	else
	{
		if(p->len + len + 1 > 8192) /* This is a little much... */
		{
			sasl_sts(p->uid, 'D', "F");
			destroy_session(p);
			return;
		}

		p->buf = (char *)realloc(p->buf, p->len + len + 1);
		p->p = p->buf + p->len;
		p->len += len;
	}

	memcpy(p->p, smsg->buf, len);

	/* Messages not exactly 400 bytes are the end of a packet. */
	if(len < 400)
	{
		p->buf[p->len] = '\0';
		sasl_packet(p, p->buf, p->len);
		free(p->buf);
		p->buf = p->p = NULL;
		p->len = 0;
	}
}

/* find a mechanism by name */
static sasl_mechanism_t *find_mechanism(char *name)
{
	node_t *n;
	sasl_mechanism_t *mptr;

	LIST_FOREACH(n, sasl_mechanisms.head)
	{
		mptr = n->data;
		if(!strcmp(mptr->name, name))
			return mptr;
	}

	slog(LG_DEBUG, "find_mechanism(): cannot find mechanism `%s'!", name);

	return NULL;
}

/* given an entire sasl message, advance session by passing data to mechanism
 * and feeding returned data back to client.
 */
static void sasl_packet(sasl_session_t *p, char *buf, int len)
{
	int rc;
	size_t tlen = 0;
	char *cloak, *out = NULL;
	char *temp;
	char mech[21];
	int out_len = 0;
	metadata_t *md;

	/* First piece of data in a session is the name of
	 * the SASL mechanism that will be used.
	 */
	if(!p->mechptr)
	{
		if(len > 20)
		{
			sasl_sts(p->uid, 'D', "F");
			destroy_session(p);
			return;
		}

		memcpy(mech, buf, len);
		mech[len] = '\0';

		if(!(p->mechptr = find_mechanism(mech)))
		{
			/* Generate a list of supported mechanisms (disabled since charybdis doesn't support this yet). */
/*			
			char temp[400], *ptr = temp;
			int l = 0;
			node_t *n;

			LIST_FOREACH(n, sasl_mechanisms.head)
			{
				sasl_mechanism_t *mptr = n->data;
				if(l + strlen(mptr->name) > 510)
					break;
				strcpy(ptr, mptr->name);
				ptr += strlen(mptr->name);
				*ptr++ = ',';
				l += strlen(mptr->name) + 1;
			}

			if(l)
				ptr--;
			*ptr = '\0';

			sasl_sts(p->uid, 'M', temp);
			*/

			sasl_sts(p->uid, 'D', "F");
			destroy_session(p);
			return;
		}

		rc = p->mechptr->mech_start(p, &out, &out_len);
	}else{
		if(base64_decode_alloc(buf, len, &temp, &tlen))
		{
			rc = p->mechptr->mech_step(p, temp, tlen, &out, &out_len);
			free(temp);
		}else
			rc = ASASL_FAIL;
	}

	/* Some progress has been made, reset timeout. */
	p->flags &= ~ASASL_MARKED_FOR_DELETION;

	if(rc == ASASL_DONE)
	{
		myuser_t *mu = myuser_find(p->username);
		if(mu && login_user(p))
		{
			if ((md = metadata_find(mu, "private:usercloak")))
				cloak = md->value;
			else
				cloak = "*";

			svslogin_sts(p->uid, "*", "*", cloak, mu->name);
			sasl_sts(p->uid, 'D', "S");
		}
		else
			sasl_sts(p->uid, 'D', "F");
		/* Will destroy session on introduction of user to net. */
		return;
	}
	else if(rc == ASASL_MORE)
	{
		if(out_len)
		{
			if(base64_encode_alloc(out, out_len, &temp))
			{
				sasl_write(p->uid, temp, strlen(temp));
				free(temp);
				free(out);
				return;
			}
		}
		else
		{
			sasl_sts(p->uid, 'C', "+");
			free(out);
			return;
		}
	}

	free(out);
	sasl_sts(p->uid, 'D', "F");
	destroy_session(p);
}

/* output an arbitrary amount of data to the SASL client */
static void sasl_write(char *target, char *data, int length)
{
	char out[401];
	int last = 400, rem = length;

	while(rem)
	{
		int nbytes = rem > 400 ? 400 : rem;
		memcpy(out, data, nbytes);
		out[nbytes] = '\0';
		sasl_sts(target, 'C', out);

		data += nbytes;
		rem -= nbytes;
		last = nbytes;
	}
	
	/* The end of a packet is indicated by a string not of length 400.
	 * If last piece is exactly 400 in size, send an empty string to
	 * finish the transaction.
	 * Also if there is no data at all.
	 */
	if(last == 400)
		sasl_sts(target, 'C', "+");
}

static void sasl_logcommand(sasl_session_t *p, myuser_t *login, int level, const char *fmt, ...)
{
	va_list args;
	char lbuf[BUFSIZE];
	
	va_start(args, fmt);
	vsnprintf(lbuf, BUFSIZE, fmt, args);
	slog(level, "%s %s:%s %s", saslsvs.nick, login ? login->name : "",
			p->uid, lbuf);
	va_end(args);
}

/* authenticated, now double check that their account is ok for login */
int login_user(sasl_session_t *p)
{
	myuser_t *mu = myuser_find(p->username);
	metadata_t *md;

	if(mu == NULL) /* WTF? */
		return 0;

 	if ((md = metadata_find(mu, "private:freeze:freezer")))
	{
		sasl_logcommand(p, NULL, CMDLOG_LOGIN, "failed LOGIN to %s (frozen)", mu->name);
		return 0;
	}

	if (LIST_LENGTH(&mu->logins) >= me.maxlogins)
	{
		sasl_logcommand(p, NULL, CMDLOG_LOGIN, "failed LOGIN to %s (too many logins)", mu->name);
		return 0;
	}

	/* Log it with the full n!u@h later */
	p->flags |= ASASL_NEED_LOG;

	return 1;
}

/* clean up after a user who is finally on the net */
static void sasl_newuser(user_t *u)
{
	sasl_session_t *p = find_session(u->uid);
	metadata_t *md_failnum;
	char lau[BUFSIZE], lao[BUFSIZE];
	char strfbuf[BUFSIZE];
	struct tm tm;
	myuser_t *mu;
	node_t *n;

	/* Not concerned unless it's a SASL login. */
	if(p == NULL)
		return;

	/* We will log it ourselves, if needed */
	p->flags &= ~ASASL_NEED_LOG;

	/* Find the account */
	mu = p->username ? myuser_find(p->username) : NULL;
	if (mu == NULL)
	{
		notice(saslsvs.nick, u->nick, "Account %s dropped, login cancelled",
				p->username ? p->username : "??");
		destroy_session(p);
		/* We'll remove their ircd login in handle_burstlogin() */
		return;
	}
	destroy_session(p);

	if (is_soper(mu))
	{
		snoop("SOPER: \2%s\2 as \2%s\2", u->nick, mu->name);
	}

	myuser_notice(saslsvs.nick, mu, "%s!%s@%s has just authenticated as you (%s)", u->nick, u->user, u->vhost, mu->name);

	u->myuser = mu;
	n = node_create();
	node_add(u, n, &mu->logins);

	/* keep track of login address for users */
	strlcpy(lau, u->user, BUFSIZE);
	strlcat(lau, "@", BUFSIZE);
	strlcat(lau, u->vhost, BUFSIZE);
	metadata_add(mu, "private:host:vhost", lau);

	/* and for opers */
	strlcpy(lao, u->user, BUFSIZE);
	strlcat(lao, "@", BUFSIZE);
	/* Hack for charybdis before 2.1: store IP instead of vhost
	 * (real host is not known at this time) -- jilles */
	slog(LG_DEBUG, "nick %s host %s vhost %s ip %s",
			u->nick, u->host, u->vhost, u->ip);
	if (!strcmp(u->host, u->vhost) && *u->ip != '\0' &&
			metadata_find(mu, "private:usercloak"))
		strlcat(lao, u->ip, BUFSIZE);
	else
		strlcat(lao, u->host, BUFSIZE);
	metadata_add(mu, "private:host:actual", lao);

	logcommand_user(saslsvs.me, u, CMDLOG_LOGIN, "LOGIN");

	/* check for failed attempts and let them know */
	if ((md_failnum = metadata_find(mu, "private:loginfail:failnum")) && (atoi(md_failnum->value) > 0))
	{
		metadata_t *md_failtime, *md_failaddr;
		time_t ts;

		notice(saslsvs.nick, u->nick, "\2%d\2 failed %s since last login.",
			atoi(md_failnum->value), (atoi(md_failnum->value) == 1) ? "login" : "logins");

		md_failtime = metadata_find(mu, "private:loginfail:lastfailtime");
		ts = atol(md_failtime->value);
		md_failaddr = metadata_find(mu, "private:loginfail:lastfailaddr");

		tm = *localtime(&ts);
		strftime(strfbuf, sizeof(strfbuf) - 1, "%b %d %H:%M:%S %Y", &tm);

		notice(saslsvs.nick, u->nick, "Last failed attempt from: \2%s\2 on %s.",
			md_failaddr->value, strfbuf);

		metadata_delete(mu, "private:loginfail:failnum");	/* md_failnum now invalid */
		metadata_delete(mu, "private:loginfail:lastfailtime");
		metadata_delete(mu, "private:loginfail:lastfailaddr");
	}

	mu->lastlogin = CURRTIME;
	hook_call_user_identify(u);
}

/* This function is run approximately once every 30 seconds.
 * It looks for flagged sessions, and deletes them, while
 * flagging all the others. This way stale sessions are deleted
 * after no more than 60 seconds.
 */
static void delete_stale(void *vptr)
{
	sasl_session_t *p;
	node_t *n, *tn;

	LIST_FOREACH_SAFE(n, tn, sessions.head)
	{
		p = n->data;
		if(p->flags & ASASL_MARKED_FOR_DELETION)
		{
			node_del(n, &sessions);
			destroy_session(p);
			node_free(n);
		} else
			p->flags |= ASASL_MARKED_FOR_DELETION;
	}
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
