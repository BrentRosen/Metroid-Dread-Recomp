/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The launcher's languages are named by BCP 47 tags (en, pt-BR, zh-Hans-CN), and the system names its own by locales
   in more forms than one (pt_BR.UTF-8, pt-BR, zh-Hant_TW, zh_TW). Both read as language[-Script][-REGION]: '-' or '_'
   between the parts, letters of any case, a script of 4 letters, a region of 2 letters or 3 digits; what follows them
   (a variant, .UTF-8, @euro) is left out */
#ifndef SNX_COMMON_LANGUAGE_TAG_H
#define SNX_COMMON_LANGUAGE_TAG_H

/* how well the language of tag is the one locale asks for: 3 the same language, script and region (or neither naming
   a region); 2 the same language and script, tag naming no region; 1 the same language and script, the locale naming
   another region or none; 0 another language, or another script. A script left out is the one its language and region
   imply (zh-TW: Hant, zh-CN: Hans), and none for a language written in one */
int snx_language_fit(const char* tag, const char* locale);
#endif
