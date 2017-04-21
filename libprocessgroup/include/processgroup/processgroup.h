/*
 *  Copyright 2014 Google, Inc
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef _PROCESSGROUP_H_
#define _PROCESSGROUP_H_

#include <sys/cdefs.h>
#include <sys/types.h>

__BEGIN_DECLS

int countProcessesInProcessGroup(uid_t uid, int initialPid);

int killProcessGroup(uid_t uid, int initialPid, int signal);

// killProcessGroupOnce sends its signal but doesn't wait to see if any processes were
// killed due to it, therefore it returns no error code.
void killProcessGroupOnce(uid_t uid, int initialPid, int signal);

int createProcessGroup(uid_t uid, int initialPid);

void removeAllProcessGroups(void);

int removeProcessGroup(uid_t uid, int pid);

__END_DECLS

#endif
