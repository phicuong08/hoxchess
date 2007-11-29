/***************************************************************************
 *  Copyright 2007 Huy Phan  <huyphan@playxiangqi.com>                     *
 *                                                                         * 
 *  This file is part of HOXChess.                                         *
 *                                                                         *
 *  HOXChess is free software: you can redistribute it and/or modify       *
 *  it under the terms of the GNU General Public License as published by   *
 *  the Free Software Foundation, either version 3 of the License, or      *
 *  (at your option) any later version.                                    *
 *                                                                         *
 *  HOXChess is distributed in the hope that it will be useful,            *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *  GNU General Public License for more details.                           *
 *                                                                         *
 *  You should have received a copy of the GNU General Public License      *
 *  along with HOXChess.  If not, see <http://www.gnu.org/licenses/>.      *
 ***************************************************************************/

/////////////////////////////////////////////////////////////////////////////
// Name:            hoxPlayerMgr.h
// Created:         10/06/2007
//
// Description:     The manager that manages ALL the tables in this server.
/////////////////////////////////////////////////////////////////////////////

#ifndef __INCLUDED_HOX_PLAYER_MGR_H_
#define __INCLUDED_HOX_PLAYER_MGR_H_

#include "wx/wx.h"
#include "hoxPlayer.h"
#include "hoxHostPlayer.h"
#include "hoxRemotePlayer.h"
#include "hoxHttpPlayer.h"
#include "hoxMyPlayer.h"

/* Forward declarations */
class hoxSite;

/**
 * A singleton class that manages all players in the system.
 */
class hoxPlayerMgr
{
public:
    static hoxPlayerMgr* GetInstance();        

    hoxPlayerMgr();
    ~hoxPlayerMgr();

    hoxHostPlayer* CreateHostPlayer( const wxString& name,
                                     int             score = 1500 );

    hoxHttpPlayer* CreateHTTPPlayer( const wxString& name,
                                     int             score = 1500 );

    hoxMyPlayer* CreateMyPlayer( const wxString& name,
                                 int             score = 1500 );

    hoxRemotePlayer* CreateRemotePlayer( const wxString& name,
                                         int             score = 1500 );

    hoxPlayer* CreateDummyPlayer( const wxString& name,
                                  int             score = 1500 );

    void DeletePlayer( hoxPlayer* player );

    /**
     * Remove a given player from the list only, do not release it memory.
     * FIXME: We need to look into this further.
     */
    int RemovePlayer( hoxPlayer* player );

    /**
     * @return NULL if not found.
     */
    hoxPlayer* FindPlayer( const wxString& playerId ) const;

    /**
     * Handle SHUTDOWN request from App.
     */
    void OnSystemShutdown();

    /**
     * Return the number of players in the system.
     */
    int GetNumberOfPlayers() { return (int) m_players.size(); }

    void     SetSite(hoxSite* site) { m_site = site; }
    hoxSite* GetSite() const        { return m_site; }

private:
    static hoxPlayerMgr* m_instance;  // The single instance

    hoxSite*        m_site;
    hoxPlayerList   m_players;  // The list of all players in the system.
};

#endif /* __INCLUDED_HOX_PLAYER_MGR_H_ */
