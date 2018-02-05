#ifndef _SOCKET_MGR_H_
#define _SOCKET_MGR_H_

struct socket_mgr_state;

struct socket_mgr_state* socket_mgr_create();
void  socket_mgr_release(struct socket_mgr_state* state);
void  socket_mgr_update(struct socket_mgr_state* state);

#endif