/*
 * Copyright (c) 2003-2004 E. Will et al.
 * Copyright (c) 2005-2006 Atheme Development Group
 * Rights to this code are documented in doc/LICENSE.
 *
 * This file contains reverse-engineered IRCXPRO 1.2/OfficeIRC support.
 *
 * $Id: officeirc.c 8301 2007-05-20 13:22:15Z jilles $
 */

#include "atheme.h"
#include "uplink.h"
#include "pmodule.h"
#include "protocol/officeirc.h"

DECLARE_MODULE_V1("protocol/officeirc", TRUE, _modinit, NULL, "$Id: officeirc.c 8301 2007-05-20 13:22:15Z jilles $", "Atheme Development Group <http://www.atheme.org>");

/* *INDENT-OFF* */

ircd_t officeirc = {
        "officeirc/ircxpro",            /* IRCd name */
        "$",                            /* TLD Prefix, used by Global. */
        FALSE,                          /* Whether or not we use IRCNet/TS6 UID */
        FALSE,                          /* Whether or not we use RCOMMAND */
        TRUE,                           /* Whether or not we support channel owners. */
        FALSE,                          /* Whether or not we support channel protection. */
        FALSE,                          /* Whether or not we support halfops. */
	FALSE,				/* Whether or not we use P10 */
	FALSE,				/* Whether or not we use vHosts. */
	0,				/* Oper-only cmodes */
        0,                              /* Integer flag for owner channel flag. */
        0,                              /* Integer flag for protect channel flag. */
        0,                              /* Integer flag for halfops. */
        "+q",                           /* Mode we set for owner. */
        "+",                            /* Mode we set for protect. */
        "+",                            /* Mode we set for halfops. */
	PROTOCOL_OFFICEIRC,		/* Protocol type */
	0,                              /* Permanent cmodes */
	0,                              /* Oper-immune cmode */
	"b",                            /* Ban-like cmodes */
	0,                              /* Except mchar */
	0,                              /* Invex mchar */
	0                               /* Flags */
};

struct cmode_ officeirc_mode_list[] = {
  { 'i', CMODE_INVITE   },
  { 'm', CMODE_MOD      },
  { 'n', CMODE_NOEXT    },
  { 'p', CMODE_PRIV     },
  { 's', CMODE_SEC      },
  { 't', CMODE_TOPIC    },
  { 'c', CMODE_NOCOLOR  },
  { 'R', CMODE_REGONLY  },
  { '\0', 0 }
};

struct extmode officeirc_ignore_mode_list[] = {
  { '\0', 0 }
};

struct cmode_ officeirc_status_mode_list[] = {
  { 'q', CMODE_OWNER | CMODE_OP  },
  { 'o', CMODE_OP      },
  { 'v', CMODE_VOICE   },
  { '\0', 0 }
};

struct cmode_ officeirc_prefix_mode_list[] = {
  { '.', CMODE_OWNER | CMODE_OP  },
  { '@', CMODE_OP      },
  { '+', CMODE_VOICE   },
  { '\0', 0 }
};

struct cmode_ officeirc_user_mode_list[] = {
  { 'i', UF_INVIS    },
  { 'o', UF_IRCOP    },
  { '\0', 0 }
};

/* *INDENT-ON* */

/* login to our uplink */
static unsigned int officeirc_server_login(void)
{
	int ret;

	ret = sts("PASS %s", curr_uplink->pass);
	if (ret == 1)
		return 1;

	me.bursting = TRUE;

	sts("SERVER %s 1 :%s", me.name, me.desc);

	services_init();

	return 0;
}

/* introduce a client */
static void officeirc_introduce_nick(user_t *u)
{
	/* NICK nenolod 1 1171690800 ~nenolod 69.60.119.15[69.60.119.15] ircxserver01 0 :Unknown */
	const char *omode = is_ircop(u) ? "o" : "";

	sts("NICK %s 1 %lu %s %s[0.0.0.0] %s 0 :%s", u->nick, (unsigned long)u->ts, u->user, u->host, me.name, u->gecos);
	sts(":%s MODE %s +i%s", u->nick, u->nick, omode);
}

/* invite a user to a channel */
static void officeirc_invite_sts(user_t *sender, user_t *target, channel_t *channel)
{
	sts(":%s INVITE %s %s", sender->nick, target->nick, channel->name);
}

static void officeirc_quit_sts(user_t *u, const char *reason)
{
	if (!me.connected)
		return;

	sts(":%s QUIT :%s", u->nick, reason);
}

/* WALLOPS wrapper */
static void officeirc_wallops_sts(const char *text)
{
	sts(":%s GLOBOPS :%s", me.name, text);
}

/* join a channel */
static void officeirc_join_sts(channel_t *c, user_t *u, boolean_t isnew, char *modes)
{
	if (isnew)
	{
		sts(":%s JOIN %s", u->nick, c->name);
		if (modes[0] && modes[1])
			sts(":%s MODE %s %s", u->nick, c->name, modes);
	}
	else
	{
		sts(":%s JOIN %s", u->nick, c->name);
	}
	sts(":%s MODE %s +q %s", u->nick, c->name, u->nick);
}

/* kicks a user from a channel */
static void officeirc_kick(user_t *source, channel_t *c, user_t *u, const char *reason)
{
	sts(":%s KICK %s %s :%s", source->nick, c->name, u->nick, reason);

	chanuser_delete(c, u);
}

/* PRIVMSG wrapper */
static void officeirc_msg(const char *from, const char *target, const char *fmt, ...)
{
	va_list ap;
	char buf[BUFSIZE];

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZE, fmt, ap);
	va_end(ap);

	sts(":%s PRIVMSG %s :%s", from, target, buf);
}

/* NOTICE wrapper */
static void officeirc_notice_user_sts(user_t *from, user_t *target, const char *text)
{
	sts(":%s NOTICE %s :%s", from ? from->nick : me.name, target->nick, text);
}

static void officeirc_notice_global_sts(user_t *from, const char *mask, const char *text)
{
	node_t *n;
	tld_t *tld;

	if (!strcmp(mask, "*"))
	{
		LIST_FOREACH(n, tldlist.head)
		{
			tld = n->data;
			sts(":%s NOTICE %s*%s :%s", from ? from->nick : me.name, ircd->tldprefix, tld->name, text);
		}
	}
	else
		sts(":%s NOTICE %s%s :%s", from ? from->nick : me.name, ircd->tldprefix, mask, text);
}

static void officeirc_notice_channel_sts(user_t *from, channel_t *target, const char *text)
{
	sts(":%s NOTICE %s :%s", from ? from->nick : me.name, target->name, text);
}

static void officeirc_numeric_sts(server_t *from, int numeric, user_t *target, const char *fmt, ...)
{
	va_list ap;
	char buf[BUFSIZE];

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZE, fmt, ap);
	va_end(ap);

	sts(":%s %d %s %s", from->name, numeric, target->nick, buf);
}

/* KILL wrapper */
static void officeirc_kill_id_sts(user_t *killer, const char *id, const char *reason)
{
	if (killer != NULL)
		sts(":%s KILL %s :%s!%s (%s)", killer->nick, id, killer->host, killer->nick, reason);
	else
		sts(":%s KILL %s :%s (%s)", me.name, id, me.name, reason);
}

/* PART wrapper */
static void officeirc_part_sts(channel_t *c, user_t *u)
{
	sts(":%s PART %s", u->nick, c->name);
}

/* server-to-server KLINE wrapper */
static void officeirc_kline_sts(char *server, char *user, char *host, long duration, char *reason)
{
	if (!me.connected)
		return;

	sts(":%s ACCESS * ADD AKILL 0 *!%s@%s$* %lu 0 %s %ld 0 :%s", 
		opersvs.nick, host, user, (unsigned long)CURRTIME, 
		opersvs.nick, duration, reason);
}

/* server-to-server UNKLINE wrapper */
static void officeirc_unkline_sts(char *server, char *user, char *host)
{
	if (!me.connected)
		return;

	sts(":%s ACCESS * DELETE AKILL 0 *!%s@%s$* %ld 0 %s 0 0 :", 
		opersvs.nick, host, user, (unsigned long)CURRTIME, 
		opersvs.nick);
}

/* topic wrapper */
static void officeirc_topic_sts(channel_t *c, const char *setter, time_t ts, time_t prevts, const char *topic)
{
	if (!me.connected)
		return;

	if (ts < prevts || prevts == 0)
		sts(":%s TOPIC %s %s %lu :%s", chansvs.nick, c->name, setter, (unsigned long)ts, topic);
	else if (prevts > 1)
	{
		ts = prevts - 1;
		sts(":%s TOPIC %s %s %lu :%s", chansvs.nick, c->name, "topictime.wrong", (unsigned long)ts, topic);
		c->topicts = ts;
	}
	else
	{
		notice(chansvs.nick, c->name, "Unable to change topic to: %s", topic);
		c->topicts = 1;
	}
}

/* mode wrapper */
static void officeirc_mode_sts(char *sender, channel_t *target, char *modes)
{
	if (!me.connected)
		return;

	sts(":%s MODE %s %s", sender, target->name, modes);
}

/* ping wrapper */
static void officeirc_ping_sts(void)
{
	if (!me.connected)
		return;

	sts("PING :%s", me.name);
}

/* protocol-specific stuff to do on login */
static void officeirc_on_login(char *origin, char *user, char *wantedhost)
{
	user_t *u = user_find(origin);

	if (!me.connected || u == NULL)
		return;

	/* Can only do this for nickserv, and can only record identified
	 * state if logged in to correct nick, sorry -- jilles
	 */
	if (should_reg_umode(u))
		sts(":%s SVSMODE %s +rd %lu", nicksvs.nick, origin, (unsigned long)CURRTIME);
}

/* protocol-specific stuff to do on login */
static boolean_t officeirc_on_logout(char *origin, char *user, char *wantedhost)
{
	if (!me.connected)
		return FALSE;

	if (!nicksvs.no_nick_ownership)
		sts(":%s SVSMODE %s -r+d %lu", nicksvs.nick, origin, (unsigned long)CURRTIME);
	return FALSE;
}

static void officeirc_jupe(const char *server, const char *reason)
{
	if (!me.connected)
		return;

	server_delete(server);
	sts(":%s SQUIT %s :%s", opersvs.nick, server, reason);
	sts(":%s SERVER %s 2 :%s", me.name, server, reason);
}

static void m_topic(sourceinfo_t *si, int parc, char *parv[])
{
	channel_t *c = channel_find(parv[0]);

	if (!c)
		return;

	handle_topic_from(si, c, parv[1], atol(parv[2]), parv[3]);
}

static void m_ping(sourceinfo_t *si, int parc, char *parv[])
{
	/* reply to PING's */
	sts(":%s PONG %s %s", me.name, me.name, parv[0]);
}

static void m_pong(sourceinfo_t *si, int parc, char *parv[])
{
	server_t *s;

	/* someone replied to our PING */
	if (!parv[0])
		return;
	s = server_find(parv[0]);
	if (s == NULL)
		return;
	handle_eob(s);

	if (irccasecmp(me.actual, parv[0]))
		return;

	me.uplinkpong = CURRTIME;

	/* -> :test.projectxero.net PONG test.projectxero.net :shrike.malkier.net */
	if (me.bursting)
	{
#ifdef HAVE_GETTIMEOFDAY
		e_time(burstime, &burstime);

		slog(LG_INFO, "m_pong(): finished synching with uplink (%d %s)", (tv2ms(&burstime) > 1000) ? (tv2ms(&burstime) / 1000) : tv2ms(&burstime), (tv2ms(&burstime) > 1000) ? "s" : "ms");

		wallops("Finished synching to network in %d %s.", (tv2ms(&burstime) > 1000) ? (tv2ms(&burstime) / 1000) : tv2ms(&burstime), (tv2ms(&burstime) > 1000) ? "s" : "ms");
#else
		slog(LG_INFO, "m_pong(): finished synching with uplink");
		wallops("Finished synching to network.");
#endif

		me.bursting = FALSE;
	}
}

static void m_privmsg(sourceinfo_t *si, int parc, char *parv[])
{
	if (parc != 2)
		return;

	handle_message(si, parv[0], FALSE, parv[1]);
}

static void m_notice(sourceinfo_t *si, int parc, char *parv[])
{
	if (parc != 2)
		return;

	handle_message(si, parv[0], TRUE, parv[1]);
}

static void m_part(sourceinfo_t *si, int parc, char *parv[])
{
	int chanc;
	char *chanv[256];
	int i;

	chanc = sjtoken(parv[0], ',', chanv);
	for (i = 0; i < chanc; i++)
	{
		slog(LG_DEBUG, "m_part(): user left channel: %s -> %s", si->su->nick, chanv[i]);

		chanuser_delete(channel_find(chanv[i]), si->su);
	}
}

static void m_nick(sourceinfo_t *si, int parc, char *parv[])
{
	server_t *s;
	boolean_t realchange;

	if (parc == 8)
	{
		char *host, *ip;

		s = server_find(parv[5]);
		if (!s)
		{
			slog(LG_DEBUG, "m_nick(): new user on nonexistant server: %s", parv[6]);
			return;
		}

		slog(LG_DEBUG, "m_nick(): new user on `%s': %s", s->name, parv[0]);

		host = sstrdup(parv[4]);
		ip = strchr(host, '[');

		if (ip != NULL)
		{
			*ip++ = '\0';
			ip = strchr(ip, ']');

			if (ip != NULL)
				*ip = '\0';
		}

		user_add(parv[0], parv[3], host, ip, NULL, NULL, parv[7], s, atoi(parv[2]));

		free(host);

		/* Note: cannot rely on umode +r to see if they're identified
		 * -- jilles */

		handle_nickchange(user_find(parv[0]));
	}

	/* if it's only 2 then it's a nickname change */
	else if (parc == 2)
	{
                if (!si->su)
                {       
                        slog(LG_DEBUG, "m_nick(): server trying to change nick: %s", si->s != NULL ? si->s->name : "<none>");
                        return;
                }

		slog(LG_DEBUG, "m_nick(): nickname change from `%s': %s", si->su->nick, parv[0]);

		realchange = irccasecmp(si->su->nick, parv[0]);

		if (user_changenick(si->su, parv[0], atoi(parv[1])))
			return;

		/* fix up +r if necessary -- jilles */
		if (realchange && !nicksvs.no_nick_ownership)
		{
			if (should_reg_umode(si->su))
				/* changed nick to registered one, reset +r */
				sts(":%s SVSMODE %s +rd %lu", nicksvs.nick, parv[0], (unsigned long)CURRTIME);
			else
				/* changed from registered nick, remove +r */
				sts(":%s SVSMODE %s -r+d %lu", nicksvs.nick, parv[0], (unsigned long)CURRTIME);
		}

		handle_nickchange(si->su);
	}
	else
	{
		int i;
		slog(LG_DEBUG, "m_nick(): got NICK with wrong number of params");

		for (i = 0; i < parc; i++)
			slog(LG_DEBUG, "m_nick():   parv[%d] = %s", i, parv[i]);
	}
}

static void m_quit(sourceinfo_t *si, int parc, char *parv[])
{
	slog(LG_DEBUG, "m_quit(): user leaving: %s", si->su->nick);

	/* user_delete() takes care of removing channels and so forth */
	user_delete(si->su);
}

static void m_mode(sourceinfo_t *si, int parc, char *parv[])
{
	if (*parv[0] == '#')
		channel_mode(NULL, channel_find(parv[0]), parc - 1, &parv[1]);
	else
		user_mode(user_find(parv[0]), parv[1]);
}

static void m_kick(sourceinfo_t *si, int parc, char *parv[])
{
	user_t *u = user_find(parv[1]);
	channel_t *c = channel_find(parv[0]);

	/* -> :rakaur KICK #shrike rintaun :test */
	slog(LG_DEBUG, "m_kick(): user was kicked: %s -> %s", parv[1], parv[0]);

	if (!u)
	{
		slog(LG_DEBUG, "m_kick(): got kick for nonexistant user %s", parv[1]);
		return;
	}

	if (!c)
	{
		slog(LG_DEBUG, "m_kick(): got kick in nonexistant channel: %s", parv[0]);
		return;
	}

	if (!chanuser_find(c, u))
	{
		slog(LG_DEBUG, "m_kick(): got kick for %s not in %s", u->nick, c->name);
		return;
	}

	chanuser_delete(c, u);

	/* if they kicked us, let's rejoin */
	if (is_internal_client(u))
	{
		slog(LG_DEBUG, "m_kick(): %s got kicked from %s; rejoining", u->nick, parv[0]);
		join(parv[0], u->nick);
	}
}

static void m_kill(sourceinfo_t *si, int parc, char *parv[])
{
	handle_kill(si, parv[0], parc > 1 ? parv[1] : "<No reason given>");
}

static void m_squit(sourceinfo_t *si, int parc, char *parv[])
{
	slog(LG_DEBUG, "m_squit(): server leaving: %s from %s", parv[0], parv[1]);
	server_delete(parv[0]);
}

static void m_server(sourceinfo_t *si, int parc, char *parv[])
{
	server_t *s;

	slog(LG_DEBUG, "m_server(): new server: %s", parv[0]);
	s = handle_server(si, parv[0], NULL, atoi(parv[1]), parv[2]);

	if (s != NULL && s->uplink != me.me)
	{
		/* elicit PONG for EOB detection; pinging uplink is
		 * already done elsewhere -- jilles
		 */
		sts(":%s PING %s %s", me.name, me.name, s->name);
	}
}

static void m_stats(sourceinfo_t *si, int parc, char *parv[])
{
	handle_stats(si->su, parv[0][0]);
}

static void m_admin(sourceinfo_t *si, int parc, char *parv[])
{
	handle_admin(si->su);
}

static void m_version(sourceinfo_t *si, int parc, char *parv[])
{
	handle_version(si->su);
}

static void m_info(sourceinfo_t *si, int parc, char *parv[])
{
	handle_info(si->su);
}

static void m_whois(sourceinfo_t *si, int parc, char *parv[])
{
	handle_whois(si->su, parv[1]);
}

static void m_trace(sourceinfo_t *si, int parc, char *parv[])
{
	handle_trace(si->su, parv[0], parc >= 2 ? parv[1] : NULL);
}

static void m_away(sourceinfo_t *si, int parc, char *parv[])
{
	handle_away(si->su, parc >= 1 ? parv[0] : NULL);
}

static void m_join(sourceinfo_t *si, int parc, char *parv[])
{
	chanuser_t *cu;
	node_t *n, *tn;
	int chanc;
	char *chanv[256];
	int i;

	/* JOIN 0 is really a part from all channels */
	if (parv[0][0] == '0')
	{
		LIST_FOREACH_SAFE(n, tn, si->su->channels.head)
		{
			cu = (chanuser_t *) n->data;
			chanuser_delete(cu->chan, si->su);
		}
	}
	else
	{
		chanc = sjtoken(parv[0], ',', chanv);
		for (i = 0; i < chanc; i++)
		{
			channel_t *c = channel_find(chanv[i]);

			if (!c)
			{
				slog(LG_DEBUG, "m_join(): new channel: %s", parv[0]);
				c = channel_add(chanv[i], CURRTIME, si->su->server);
				/* Tell the core to check mode locks now,
				 * otherwise it may only happen after the next
				 * mode change.
				 * DreamForge does not allow any redundant modes
				 * so this will not look ugly. -- jilles */
				/* If this is in a burst, a MODE with the
				 * simple modes will follow so we can skip
				 * this. -- jilles */
				if (!me.bursting)
					channel_mode_va(NULL, c, 1, "+");
			}
			chanuser_add(c, si->su->nick);
		}
	}
}

static void m_pass(sourceinfo_t *si, int parc, char *parv[])
{
	if (strcmp(curr_uplink->pass, parv[0]))
	{
		slog(LG_INFO, "m_pass(): password mismatch from uplink; aborting");
		runflags |= RF_SHUTDOWN;
	}
}

static void m_error(sourceinfo_t *si, int parc, char *parv[])
{
	slog(LG_INFO, "m_error(): error from server: %s", parv[0]);
}

static void m_motd(sourceinfo_t *si, int parc, char *parv[])
{
	handle_motd(si->su);
}

static void m_njoin(sourceinfo_t *si, int parc, char *parv[])
{
	/* -> :ircxserver01 NJOIN #take 1073516550 :.qbot */

	channel_t *c;
	unsigned int userc;
	char *userv[256];
	unsigned int i;
	time_t ts;
	char *p;
	boolean_t keep_new_modes = TRUE;

	/* :origin SJOIN chan ts :users */
	if (si->s == NULL)
		return;

	c = channel_find(parv[0]);
	ts = atol(parv[1]);

	if (!c)
	{
		slog(LG_DEBUG, "m_njoin(): new channel: %s", parv[1]);
		c = channel_add(parv[0], ts, si->s);
		/* Tell the core to check mode locks now,
		 * otherwise it may only happen after the next
		 * mode change.
		 * DreamForge does not allow any redundant modes
		 * so this will not look ugly. -- jilles */
		channel_mode_va(NULL, c, 1, "+");
	}

	if (ts == 0 || c->ts == 0)
	{
		if (c->ts != 0)
			slog(LG_INFO, "m_sjoin(): server %s changing TS on %s from %ld to 0", si->s->name, c->name, (long)c->ts);
		c->ts = 0;
		hook_call_event("channel_tschange", c);
	}
	else if (ts < c->ts)
	{
		chanuser_t *cu;
		node_t *n;

		/* the TS changed.  a TS change requires the following things
		 * to be done to the channel:  reset all modes to nothing, remove
		 * all status modes on known users on the channel (including ours),
		 * and set the new TS.
		 */

		clear_simple_modes(c);

		LIST_FOREACH(n, c->members.head)
		{
			cu = (chanuser_t *)n->data;
			if (cu->user->server == me.me)
			{
				/* it's a service, reop */
				sts(":%s MODE %s +o %s", CLIENT_NAME(cu->user), c->name, CLIENT_NAME(cu->user));
				cu->modes = CMODE_OP;
			}
			else
				cu->modes = 0;
		}

		slog(LG_DEBUG, "m_sjoin(): TS changed for %s (%lu -> %lu)", c->name, (unsigned long)c->ts, (unsigned long)ts);

		c->ts = ts;
		hook_call_event("channel_tschange", c);
	}
	else if (ts > c->ts)
		keep_new_modes = FALSE;

	userc = sjtoken(parv[parc - 1], ' ', userv);

	if (keep_new_modes)
		for (i = 0; i < userc; i++)
			chanuser_add(c, userv[i]);
	else
		for (i = 0; i < userc; i++)
		{
			p = userv[i];
			while (*p == '@' || *p == '%' || *p == '+' || *p == '.')
				p++;
			chanuser_add(c, p);
		}

	if (c->nummembers == 0)
		channel_delete(c);
}

static void nick_group(hook_user_req_t *hdata)
{
	user_t *u;

	u = hdata->si->su != NULL && !irccasecmp(hdata->si->su->nick, hdata->mn->nick) ? hdata->si->su : user_find_named(hdata->mn->nick);
	if (u != NULL && should_reg_umode(u))
		sts(":%s SVSMODE %s +rd %lu", nicksvs.nick, u->nick, (unsigned long)CURRTIME);
}

static void nick_ungroup(hook_user_req_t *hdata)
{
	user_t *u;

	u = hdata->si->su != NULL && !irccasecmp(hdata->si->su->nick, hdata->mn->nick) ? hdata->si->su : user_find_named(hdata->mn->nick);
	if (u != NULL && !nicksvs.no_nick_ownership)
		sts(":%s SVSMODE %s -r+d %lu", nicksvs.nick, u->nick, (unsigned long)CURRTIME);
}

void _modinit(module_t * m)
{
	/* Symbol relocation voodoo. */
	server_login = &officeirc_server_login;
	introduce_nick = &officeirc_introduce_nick;
	quit_sts = &officeirc_quit_sts;
	wallops_sts = &officeirc_wallops_sts;
	join_sts = &officeirc_join_sts;
	kick = &officeirc_kick;
	msg = &officeirc_msg;
	notice_user_sts = &officeirc_notice_user_sts;
	notice_global_sts = &officeirc_notice_global_sts;
	notice_channel_sts = &officeirc_notice_channel_sts;
	numeric_sts = &officeirc_numeric_sts;
	kill_id_sts = &officeirc_kill_id_sts;
	part_sts = &officeirc_part_sts;
	kline_sts = &officeirc_kline_sts;
	unkline_sts = &officeirc_unkline_sts;
	topic_sts = &officeirc_topic_sts;
	mode_sts = &officeirc_mode_sts;
	ping_sts = &officeirc_ping_sts;
	ircd_on_login = &officeirc_on_login;
	ircd_on_logout = &officeirc_on_logout;
	jupe = &officeirc_jupe;
	invite_sts = &officeirc_invite_sts;

	mode_list = officeirc_mode_list;
	ignore_mode_list = officeirc_ignore_mode_list;
	status_mode_list = officeirc_status_mode_list;
	prefix_mode_list = officeirc_prefix_mode_list;
	user_mode_list = officeirc_user_mode_list;

	ircd = &officeirc;

	pcommand_add("PING", m_ping, 1, MSRC_USER | MSRC_SERVER);
	pcommand_add("PONG", m_pong, 1, MSRC_SERVER);
	pcommand_add("PRIVMSG", m_privmsg, 2, MSRC_USER);
	pcommand_add("NOTICE", m_notice, 2, MSRC_UNREG | MSRC_USER | MSRC_SERVER);
	pcommand_add("PART", m_part, 1, MSRC_USER);
	pcommand_add("NICK", m_nick, 2, MSRC_USER | MSRC_SERVER);
	pcommand_add("QUIT", m_quit, 1, MSRC_USER);
	pcommand_add("MODE", m_mode, 2, MSRC_USER | MSRC_SERVER);
	pcommand_add("KICK", m_kick, 2, MSRC_USER | MSRC_SERVER);
	pcommand_add("KILL", m_kill, 1, MSRC_USER | MSRC_SERVER);
	pcommand_add("SQUIT", m_squit, 1, MSRC_USER | MSRC_SERVER);
	pcommand_add("SERVER", m_server, 3, MSRC_UNREG | MSRC_SERVER);
	pcommand_add("STATS", m_stats, 2, MSRC_USER);
	pcommand_add("ADMIN", m_admin, 1, MSRC_USER);
	pcommand_add("VERSION", m_version, 1, MSRC_USER);
	pcommand_add("INFO", m_info, 1, MSRC_USER);
	pcommand_add("WHOIS", m_whois, 2, MSRC_USER);
	pcommand_add("TRACE", m_trace, 1, MSRC_USER);
	pcommand_add("AWAY", m_away, 0, MSRC_USER);
	pcommand_add("JOIN", m_join, 1, MSRC_USER);
	pcommand_add("PASS", m_pass, 1, MSRC_UNREG);
	pcommand_add("ERROR", m_error, 1, MSRC_UNREG | MSRC_SERVER);
	pcommand_add("TOPIC", m_topic, 4, MSRC_USER | MSRC_SERVER);
	pcommand_add("MOTD", m_motd, 1, MSRC_USER);
	pcommand_add("NJOIN", m_njoin, 3, MSRC_SERVER);

	hook_add_event("nick_group");
	hook_add_hook("nick_group", (void (*)(void *))nick_group);
	hook_add_event("nick_ungroup");
	hook_add_hook("nick_ungroup", (void (*)(void *))nick_ungroup);

	m->mflags = MODTYPE_CORE;

	pmodule_loaded = TRUE;
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
