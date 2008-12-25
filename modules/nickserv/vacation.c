/*
 * Copyright (c) 2008 William Pitcock <nenolod@atheme.org>
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Allows extension of expiry times if user will be away from IRC for
 * a month or two.
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"nickserv/vacation", false, _modinit, _moddeinit,
	"$Id$",
	"Atheme Development Group <http://www.atheme.org>"
);

list_t *ns_cmdtree, *ns_helptree;

static void ns_cmd_vacation(sourceinfo_t *si, int parc, char *parv[])
{
	char tsbuf[BUFSIZE];

	if (!si->smu)
	{
		command_fail(si, fault_noprivs, _("You are not logged in."));
		return;
	}

	if (CURRTIME < (time_t)(si->smu->registered + nicksvs.expiry))
	{
		command_fail(si, fault_noprivs, _("You must be registered for at least \2%d\2 days in order to enable VACATION mode."), 
			(nicksvs.expiry / 3600 / 24));
		return;
	}

	snprintf(tsbuf, BUFSIZE, "%lu", (unsigned long)CURRTIME);
	metadata_add(si->smu, "private:vacation", tsbuf);

	logcommand(si, CMDLOG_SET, "VACATION");
	snoop("VACATION: \2%s\2", get_source_name(si));

	command_success_nodata(si, _("Your account is now marked as being on vacation.\n"
				"Please be aware that this will be automatically removed the next time you identify to \2%s\2."),
				nicksvs.nick);
	if (nicksvs.expiry > 0)
		command_success_nodata(si, _("Your account will automatically expire in %d days if you do not return."),
				(nicksvs.expiry / 3600 / 24) * 3);
}

command_t ns_vacation = { "VACATION", N_("Sets an account as being on vacation."), AC_NONE, 1, ns_cmd_vacation };

static void user_identify_hook(user_t *u)
{
	if (!metadata_find(u->myuser, "private:vacation"))
		return;

	notice(nicksvs.nick, u->nick, _("Your account is no longer marked as being on vacation."));
	metadata_delete(u->myuser, "private:vacation");
}

static void user_expiry_hook(hook_expiry_req_t *req)
{
	myuser_t *mu = req->data.mu;

	if (!metadata_find(mu, "private:vacation"))
		return;

	if (mu->lastlogin >= CURRTIME || (unsigned int)(CURRTIME - mu->lastlogin) < nicksvs.expiry * 3)
		req->do_expire = 0;
}

static void nick_expiry_hook(hook_expiry_req_t *req)
{
	mynick_t *mn = req->data.mn;
	myuser_t *mu = mn->owner;

	if (!metadata_find(mu, "private:vacation"))
		return;

	if (mu->lastlogin >= CURRTIME || (unsigned int)(CURRTIME - mu->lastlogin) < nicksvs.expiry * 3)
		req->do_expire = 0;
}

static void info_hook(hook_user_req_t *hdata)
{
	if (metadata_find(hdata->mu, "private:vacation"))
		command_success_nodata(hdata->si, "%s is on vacation and has an extended expiry time", hdata->mu->name);
}

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(ns_cmdtree, "nickserv/main", "ns_cmdtree");
	MODULE_USE_SYMBOL(ns_helptree, "nickserv/main", "ns_helptree");

	command_add(&ns_vacation, ns_cmdtree);
	help_addentry(ns_helptree, "VACATION", "help/nickserv/vacation", NULL);

	hook_add_event("user_identify");
	hook_add_hook("user_identify", (void (*)(void *))user_identify_hook);

	hook_add_event("user_check_expire");
	hook_add_hook("user_check_expire", (void (*)(void *))user_expiry_hook);

	hook_add_event("nick_check_expire");
	hook_add_hook("nick_check_expire", (void (*)(void *))nick_expiry_hook);

	hook_add_event("user_info");
	hook_add_hook("user_info", (void (*)(void *))info_hook);
}

void _moddeinit(void)
{
	command_delete(&ns_vacation, ns_cmdtree);
	help_delentry(ns_helptree, "VACATION");

	hook_del_hook("user_identify", (void (*)(void *))user_identify_hook);
	hook_del_hook("user_check_expire", (void (*)(void *))user_expiry_hook);
	hook_del_hook("nick_check_expire", (void (*)(void *))nick_expiry_hook);
	hook_del_hook("user_info", (void (*)(void *))info_hook);
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */