/***************************************************************************
 *  Copyright 2007, 2008 Huy Phan  <huyphan@playxiangqi.com>               *
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
// Name:            hoxChesscapeConnection.cpp
// Created:         12/12/2007
//
// Description:     The Socket-Connection Thread to help Chesscape player.
/////////////////////////////////////////////////////////////////////////////

#include "hoxChesscapeConnection.h"
#include "hoxLocalPlayer.h"
#include "hoxUtil.h"
#include "hoxNetworkAPI.h"

IMPLEMENT_DYNAMIC_CLASS(hoxChesscapeConnection, hoxThreadConnection)

//-----------------------------------------------------------------------------
// hoxChesscapeConnection
//-----------------------------------------------------------------------------

hoxChesscapeConnection::hoxChesscapeConnection()
{
    wxFAIL_MSG( "This default constructor is never meant to be used." );
}

hoxChesscapeConnection::hoxChesscapeConnection( const hoxServerAddress& serverAddress,
                                                hoxPlayer*              player )
        : hoxThreadConnection( serverAddress, player )
        , m_pSClient( NULL )
{
    const char* FNAME = "hoxChesscapeConnection::hoxChesscapeConnection";

    wxLogDebug("%s: Create a client-socket with default time-out = [%d] seconds.", 
        FNAME, hoxSOCKET_CLIENT_SOCKET_TIMEOUT);

    m_pSClient = new wxSocketClient( wxSOCKET_WAITALL );
    m_pSClient->Notify( false /* Disable socket-events */ );
    m_pSClient->SetTimeout( hoxSOCKET_CLIENT_SOCKET_TIMEOUT );
}

hoxChesscapeConnection::~hoxChesscapeConnection()
{
    const char* FNAME = "hoxChesscapeConnection::~hoxChesscapeConnection";

    wxLogDebug("%s: ENTER.", FNAME);

    _DestroySocket();
}

void 
hoxChesscapeConnection::HandleRequest( hoxRequest* request )
{
    const char* FNAME = "hoxChesscapeConnection::HandleRequest";
    hoxResult    result = hoxRC_ERR;
    std::auto_ptr<hoxResponse> response( new hoxResponse(request->type, 
                                                         request->sender) );

    /* 
     * SPECIAL CASE: 
     *     Handle the "special" request: Socket-Lost event,
     *     which is applicable to some requests.
     */
    if ( request->type == hoxREQUEST_PLAYER_DATA )
    {
        result = _CheckAndHandleSocketLostEvent( request, response->content );
        if ( result == hoxRC_HANDLED )
        {
            response->flags |= hoxRESPONSE_FLAG_CONNECTION_LOST;
            result = hoxRC_OK;  // Consider "success".
            goto exit_label;
        }
    }

    /*
     * NORMAL CASE: 
     *    Handle "normal" request.
     */
    switch( request->type )
    {
        case hoxREQUEST_PLAYER_DATA: // Incoming data from remote player.
        {
            wxASSERT_MSG( request->socket == m_pSClient, "Sockets should match." );
            // We disable input events until we are done processing the current command.
            hoxNetworkAPI::SocketInputLock socketLock( m_pSClient );
			wxSocketFlags savedFlags = m_pSClient->GetFlags();
			for (;;)
			{
				m_pSClient->SetFlags( savedFlags ); // Make sure to read data in the ORIGINAL mode.
				result = this->_ReadLine( m_pSClient, response->content );
				if ( result != hoxRC_OK )
				{
					wxLogError("%s: Failed to read incoming command.", FNAME);
					break; //goto exit_label;
				}

				// Peek data in advance (in non-blocking mode) to see if any more data...
				m_pSClient->SetFlags( savedFlags | wxSOCKET_NOWAIT );
				if ( m_pSClient->IsData() ) // more data to read.
				{
					// *** Perform "multiple" notifications to the Player.
					wxLogDebug("%s: Inform the Player for this command...", FNAME);
					wxCommandEvent event( hoxEVT_CONNECTION_RESPONSE, request->type );
					response->code = result;
					event.SetEventObject( response.release() );  // Caller will de-allocate.
					wxPostEvent( this->GetPlayer(), event );
					
					// *** Allocate new response.
					wxLogDebug("%s: [PLAYER_DATA] Allocate a new response.", FNAME);
					response.reset( new hoxResponse(request->type, request->sender) );
				}
				else
				{
					break; // *** Done.
				}
			} // for(...)
			// *** Restore flags.
			m_pSClient->SetFlags( savedFlags );
            break;
        }

        case hoxREQUEST_LOGIN:
		{
			const wxString login = request->parameters["pid"]; 
		    const wxString password = request->parameters["password"];
            result = _Connect(login, password, response->content);
            if ( result == hoxRC_HANDLED )
            {
                result = hoxRC_OK;  // Consider "success".
            }
			break;
		}

        case hoxREQUEST_LOGOUT:
		{
			const wxString login = request->parameters["pid"]; 
            result = _Disconnect(login);
			break;
		}

        case hoxREQUEST_JOIN:
		{
		    const wxString tableId = request->parameters["tid"];
			const bool hasRole = (request->parameters["joined"] == "1");
			const hoxColor requestColor = 
				hoxUtil::StringToColor( request->parameters["color"] );
            result = _Join(tableId, hasRole, requestColor);
			response->content = tableId;
            break;
		}

        case hoxREQUEST_PLAYER_STATUS:
		{
		    const wxString playerStatus = request->parameters["status"];
            result = _UpdateStatus( playerStatus );
            break;
		}

        case hoxREQUEST_LEAVE:
		{
            result = _Leave();
            break;
		}

        case hoxREQUEST_MOVE:
		{
            result = _Move( request );
            break;
		}

        case hoxREQUEST_NEW:
		{
            result = _New();
            break;
		}

        case hoxREQUEST_MSG:
		{
            result = _WallMessage( request );
            break;
		}

        case hoxREQUEST_DRAW:
		{
			const wxString drawResponse = request->parameters["draw_response"];
            result = _Draw( drawResponse );
            break;
		}

        default:
            wxLogError("%s: Unsupported request Type [%s].", 
                FNAME, hoxUtil::RequestTypeToString(request->type).c_str());
            result = hoxRC_NOT_SUPPORTED;
            break;
    }

exit_label:
    if ( result != hoxRC_OK )
    {
        wxLogDebug("%s: *** WARN *** Error occurred while handling request [%s].", 
            FNAME, hoxUtil::RequestTypeToString(request->type).c_str());
        response->content = "!Error_Result!";
    }

    /* NOTE: If there was error, just return it to the caller. */

    wxCommandEvent event( hoxEVT_CONNECTION_RESPONSE, request->type );
    response->code = result;
    event.SetEventObject( response.release() );  // Caller will de-allocate.
    wxPostEvent( this->GetPlayer(), event );
}

hoxResult 
hoxChesscapeConnection::_CheckAndHandleSocketLostEvent( 
                                const hoxRequest* request, 
                                wxString&         response )
{
    const char* FNAME = "hoxChesscapeConnection::_CheckAndHandleSocketLostEvent";
    hoxResult result = hoxRC_OK;

    //wxLogDebug("%s: ENTER.", FNAME);

    wxASSERT_MSG( request->socket == m_pSClient, "Sockets should match." );

    if ( request->socketEvent == wxSOCKET_LOST )
    {
        wxLogDebug("%s: Received socket-lost event. Deleting client socket.", FNAME);
        _DestroySocket();
        result = hoxRC_HANDLED;
    }

    return result;
}

hoxResult
hoxChesscapeConnection::_Connect( const wxString& login, 
		                          const wxString& password,
								  wxString&       responseStr )
{
    const char* FNAME = "hoxChesscapeConnection::_Connect";

    if ( this->IsConnected() )
    {
        wxLogDebug("%s: The connection already established. END.", FNAME);
        return hoxRC_HANDLED;
    }

    /* Get the server address. */
    wxIPV4address addr;
    addr.Hostname( m_serverAddress.name );
    addr.Service( m_serverAddress.port );

    wxLogDebug("%s: Trying to connect to [%s]...", FNAME, m_serverAddress.c_str());

    //m_pSClient->Connect( addr, false /* no-wait */ );
    //m_pSClient->WaitOnConnect( 10 /* wait for 10 seconds */ );

    if ( ! m_pSClient->Connect( addr, true /* wait */ ) )
    {
        wxLogError("%s: Failed to connect to the server [%s]. Error = [%s].",
            FNAME, m_serverAddress.c_str(), 
            hoxNetworkAPI::SocketErrorToString(m_pSClient->LastError()).c_str());
        return hoxRC_ERR;
    }

    wxLogDebug("%s: Succeeded! Connection established with the server.", FNAME);
    this->SetConnected( true );

	////////////////////////////
    // Send LOGIN request.
	{
		wxLogDebug("%s: Sending LOGIN request over the network...", FNAME);
		wxString loginRequest;
		loginRequest.Printf("\x02\x10uLogin?0\x10%s\x10%s\x10\x03", login.c_str(), password.c_str());

		wxUint32 requestSize = (wxUint32) loginRequest.size();
		m_pSClient->Write( loginRequest, requestSize );
		wxUint32 nWrite = m_pSClient->LastCount();
		if ( nWrite < requestSize )
		{
			wxLogDebug("%s: *** WARN *** Failed to send request [%s] ( %d < %d ). Error = [%s].", 
				FNAME, loginRequest.c_str(), nWrite, requestSize, 
				hoxNetworkAPI::SocketErrorToString(m_pSClient->LastError()).c_str());
			return hoxRC_ERR;
		}
	}
	////////////////////////////
	// Read the response.
	{
        hoxResult result = this->_ReadLine( m_pSClient, responseStr );
        if ( result != hoxRC_OK )
        {
            wxLogDebug("%s: *** WARN *** Failed to read incoming command.", FNAME);
            //return hoxRC_ERR;
        }
	}
	//////////////////////////////
    wxCHECK_MSG(this->GetPlayer(), hoxRC_ERR, "The player is NULL.");
    wxLogDebug("%s: Let the connection's Player [%s] handle all socket events.", 
        FNAME, this->GetPlayer()->GetName());
    m_pSClient->SetEventHandler( *(this->GetPlayer()), CLIENT_SOCKET_ID );
    m_pSClient->SetNotify(wxSOCKET_INPUT_FLAG | wxSOCKET_LOST_FLAG);
    m_pSClient->Notify(true);

    return hoxRC_OK;
}

hoxResult
hoxChesscapeConnection::_Disconnect( const wxString& login )
{
    const char* FNAME = "hoxChesscapeConnection::_Disconnect";

    if ( ! this->IsConnected() )
    {
        // NOTE: The connection could have been closed if the server is down.
        wxLogDebug("%s: Connection not yet established or has been closed.", FNAME);
        return hoxRC_OK;   // *** Fine. Do nothing.
    }

    /* Send LOGOUT request. */

	wxLogDebug("%s: Sending LOGOUT request for login [%s]...", FNAME, login.c_str());
	wxString cmdRequest;
	cmdRequest.Printf("\x02\x10%s\x10\x03", "logout?");

	wxUint32 requestSize = (wxUint32) cmdRequest.size();
	m_pSClient->Write( cmdRequest, requestSize );
	wxUint32 nWrite = m_pSClient->LastCount();
	if ( nWrite < requestSize )
	{
		wxLogDebug("%s: *** WARN *** Failed to send request [%s] ( %d < %d ). Error = [%s].", 
			FNAME, cmdRequest.c_str(), nWrite, requestSize, 
			hoxNetworkAPI::SocketErrorToString(m_pSClient->LastError()).c_str());
		return hoxRC_ERR;
	}

    return hoxRC_OK;
}

hoxResult
hoxChesscapeConnection::_Join( const wxString& tableId,
							   const bool      hasRole,
							   hoxColor   requestColor )
{
    const char* FNAME = "hoxChesscapeConnection::_Join";

    if ( ! this->IsConnected() )
    {
        // NOTE: The connection could have been closed if the server is down.
        wxLogDebug("%s: Connection not yet established or has been closed.", FNAME);
        return hoxRC_ERR;
    }

    /* Send JOIN request if the player is NOT in the table. */

	if ( ! hasRole )
	{
		wxLogDebug("%s: Sending JOIN request with table-Id = [%s]...", FNAME, tableId.c_str());
		wxString cmdRequest;
		cmdRequest.Printf("\x02\x10join?%s\x10\x03", tableId.c_str());

		wxUint32 requestSize = (wxUint32) cmdRequest.size();
		m_pSClient->Write( cmdRequest, requestSize );
		wxUint32 nWrite = m_pSClient->LastCount();
		if ( nWrite < requestSize )
		{
			wxLogDebug("%s: *** WARN *** Failed to send request [%s] ( %d < %d ). Error = [%s].", 
				FNAME, cmdRequest.c_str(), nWrite, requestSize, 
				hoxNetworkAPI::SocketErrorToString(m_pSClient->LastError()).c_str());
			return hoxRC_ERR;
		}
	}

    /* Send REQUEST-SEAT request, if asked. */
	wxString requestSeat;
	if      ( requestColor == hoxCOLOR_RED )   requestSeat = "RedSeat";
	else if ( requestColor == hoxCOLOR_BLACK ) requestSeat = "BlkSeat";

	if ( ! requestSeat.empty() )
	{
		wxLogDebug("%s: Sending REQUEST-SEAT request with seat = [%s]...", FNAME, requestSeat.c_str());
		wxString cmdRequest;
		cmdRequest.Printf("\x02\x10tCmd?%s\x10\x03", requestSeat.c_str());

		wxUint32 requestSize = (wxUint32) cmdRequest.size();
		m_pSClient->Write( cmdRequest, requestSize );
		wxUint32 nWrite = m_pSClient->LastCount();
		if ( nWrite < requestSize )
		{
			wxLogDebug("%s: *** WARN *** Failed to send request [%s] ( %d < %d ). Error = [%s].", 
				FNAME, cmdRequest.c_str(), nWrite, requestSize, 
				hoxNetworkAPI::SocketErrorToString(m_pSClient->LastError()).c_str());
			return hoxRC_ERR;
		}
	}

    return hoxRC_OK;
}

hoxResult
hoxChesscapeConnection::_UpdateStatus( const wxString& playerStatus )
{
    const char* FNAME = "hoxChesscapeConnection::_UpdateStatus";

    if ( ! this->IsConnected() )
    {
        // NOTE: The connection could have been closed if the server is down.
        wxLogDebug("%s: Connection not yet established or has been closed.", FNAME);
        return hoxRC_ERR;
    }

    /* Send UPDATE-STATUS request. */

	wxLogDebug("%s: Sending UPDATE-STATUS request with status = [%s]...", 
		FNAME, playerStatus.c_str());
	wxString cmdRequest;
	cmdRequest.Printf("\x02\x10updateStatus?%s\x10\x03", playerStatus.c_str());

	wxUint32 requestSize = (wxUint32) cmdRequest.size();
	m_pSClient->Write( cmdRequest, requestSize );
	wxUint32 nWrite = m_pSClient->LastCount();
	if ( nWrite < requestSize )
	{
		wxLogDebug("%s: *** WARN *** Failed to send request [%s] ( %d < %d ). Error = [%s].", 
			FNAME, cmdRequest.c_str(), nWrite, requestSize, 
			hoxNetworkAPI::SocketErrorToString(m_pSClient->LastError()).c_str());
		return hoxRC_ERR;
	}

    return hoxRC_OK;
}

hoxResult
hoxChesscapeConnection::_Leave()
{
    const char* FNAME = "hoxChesscapeConnection::_Leave";

    if ( ! this->IsConnected() )
    {
        // NOTE: The connection could have been closed if the server is down.
        wxLogDebug("%s: Connection not yet established or has been closed.", FNAME);
        return hoxRC_ERR;
    }

    /* Send LEAVE (table) request. */

	wxLogDebug("%s: Sending LEAVE (the current table) request...", FNAME);
	wxString cmdRequest;
	cmdRequest.Printf("\x02\x10%s\x10\x03", "closeTable?");

	wxUint32 requestSize = (wxUint32) cmdRequest.size();
	m_pSClient->Write( cmdRequest, requestSize );
	wxUint32 nWrite = m_pSClient->LastCount();
	if ( nWrite < requestSize )
	{
		wxLogDebug("%s: *** WARN *** Failed to send request [%s] ( %d < %d ). Error = [%s].", 
			FNAME, cmdRequest.c_str(), nWrite, requestSize, 
			hoxNetworkAPI::SocketErrorToString(m_pSClient->LastError()).c_str());
		return hoxRC_ERR;
	}

    return hoxRC_OK;
}

hoxResult
hoxChesscapeConnection::_New()
{
    const char* FNAME = "hoxChesscapeConnection::_New";

    if ( ! this->IsConnected() )
    {
        // NOTE: The connection could have been closed if the server is down.
        wxLogDebug("%s: Connection not yet established or has been closed.", FNAME);
        return hoxRC_ERR;
    }

    /* Send NEW (table) request. */

	wxLogDebug("%s: Sending NEW (the current table) request...", FNAME);
	wxString cmdRequest;
	cmdRequest.Printf("\x02\x10%s\x10%d\x10\x03", 
		"create?com.chesscape.server.xiangqi.TableHandler",
		0 /* Rated Table */ );

	wxUint32 requestSize = (wxUint32) cmdRequest.size();
	m_pSClient->Write( cmdRequest, requestSize );
	wxUint32 nWrite = m_pSClient->LastCount();
	if ( nWrite < requestSize )
	{
		wxLogDebug("%s: *** WARN *** Failed to send request [%s] ( %d < %d ). Error = [%s].", 
			FNAME, cmdRequest.c_str(), nWrite, requestSize, 
			hoxNetworkAPI::SocketErrorToString(m_pSClient->LastError()).c_str());
		return hoxRC_ERR;
	}

    return hoxRC_OK;
}

hoxResult   
hoxChesscapeConnection::_Move( hoxRequest* request )
{
    const char* FNAME = "hoxChesscapeConnection::_Move";

    if ( ! this->IsConnected() )
    {
        // NOTE: The connection could have been closed if the server is down.
        wxLogDebug("%s: Connection not yet established or has been closed.", FNAME);
        return hoxRC_ERR;
    }

	/* Extract parameters. */
	const wxString moveStr     = request->parameters["move"];
	const wxString statusStr   = request->parameters["status"];
	const wxString gameTimeStr = request->parameters["game_time"];
	int gameTime = ::atoi( gameTimeStr.c_str() ) * 1000;  // convert to miliseconds

    /* Send MOVE request. */

	wxLogDebug("%s: Sending MOVE [%s] request...", FNAME, moveStr.c_str());
	wxString cmdRequest;
	cmdRequest.Printf("\x02\x10tCmd?Move\x10%s\x10%d\x10\x03", 
		moveStr.c_str(), gameTime);

	wxUint32 requestSize = (wxUint32) cmdRequest.size();
	m_pSClient->Write( cmdRequest, requestSize );
	wxUint32 nWrite = m_pSClient->LastCount();
	if ( nWrite < requestSize )
	{
		wxLogDebug("%s: *** WARN *** Failed to send request [%s] ( %d < %d ). Error = [%s].", 
			FNAME, cmdRequest.c_str(), nWrite, requestSize, 
			hoxNetworkAPI::SocketErrorToString(m_pSClient->LastError()).c_str());
		return hoxRC_ERR;
	}

	/* Send GAME-STATUS request */
    const hoxGameStatus gameStatus = 
        hoxUtil::StringToGameStatus( statusStr );

	if (   gameStatus == hoxGAME_STATUS_RED_WIN 
        || gameStatus == hoxGAME_STATUS_BLACK_WIN )
	{
		wxLogDebug("%s: Sending GAME-STATUS [%s] request...", FNAME, statusStr.c_str());
		cmdRequest.Printf("\x02\x10tCmd?%s\x10\x03",
			"Winner" /* FIXME: Hard-code */);

		wxUint32 requestSize = (wxUint32) cmdRequest.size();
		m_pSClient->Write( cmdRequest, requestSize );
		wxUint32 nWrite = m_pSClient->LastCount();
		if ( nWrite < requestSize )
		{
			wxLogDebug("%s: *** WARN *** Failed to send request [%s] ( %d < %d ). Error = [%s].", 
				FNAME, cmdRequest.c_str(), nWrite, requestSize, 
				hoxNetworkAPI::SocketErrorToString(m_pSClient->LastError()).c_str());
			return hoxRC_ERR;
		}
	}

    return hoxRC_OK;
}

hoxResult   
hoxChesscapeConnection::_WallMessage( hoxRequest* request )
{
    const char* FNAME = "hoxChesscapeConnection::_WallMessage";

    if ( ! this->IsConnected() )
    {
        // NOTE: The connection could have been closed if the server is down.
        wxLogDebug("%s: Connection not yet established or has been closed.", FNAME);
        return hoxRC_ERR;
    }

	/* Extract parameters. */
	const wxString message = request->parameters["msg"];

    /* Send MESSAGE request. */

	wxLogDebug("%s: Sending MESSAGE [%s] request...", FNAME, message.c_str());
	wxString cmdRequest;
	cmdRequest.Printf("\x02\x10tMsg?%s\x10\x03", message.c_str());

	wxUint32 requestSize = (wxUint32) cmdRequest.size();
	m_pSClient->Write( cmdRequest, requestSize );
	wxUint32 nWrite = m_pSClient->LastCount();
	if ( nWrite < requestSize )
	{
		wxLogDebug("%s: *** WARN *** Failed to send request [%s] ( %d < %d ). Error = [%s].", 
			FNAME, cmdRequest.c_str(), nWrite, requestSize, 
			hoxNetworkAPI::SocketErrorToString(m_pSClient->LastError()).c_str());
		return hoxRC_ERR;
	}

    return hoxRC_OK;
}

hoxResult   
hoxChesscapeConnection::_Draw( const wxString& drawResponse )
{
    const char* FNAME = "hoxChesscapeConnection::_Draw";

    if ( ! this->IsConnected() )
    {
        // NOTE: The connection could have been closed if the server is down.
        wxLogDebug("%s: Connection not yet established or has been closed.", FNAME);
        return hoxRC_ERR;
    }

	/* Send the response to a DRAW request, if asked.
	 * Otherwise, send DRAW request. 
	 */

	wxString drawCmd;

	if      ( drawResponse == "1" )   drawCmd = "AcceptDraw";
	else if ( drawResponse.empty() )  drawCmd = "OfferDraw";
	else /* ( drawResponse == "0" ) */
	{
		// Send nothing. Done.
		wxLogDebug("%s: DRAW request is denied. Do nothing. END.", FNAME);
		return hoxRC_OK;
	}

	wxLogDebug("%s: Sending DRAW command [%s]...", FNAME, drawCmd.c_str());
	wxString cmdRequest;
	cmdRequest.Printf("\x02\x10tCmd?%s\x10\x03", drawCmd.c_str());

	wxUint32 requestSize = (wxUint32) cmdRequest.size();
	m_pSClient->Write( cmdRequest, requestSize );
	wxUint32 nWrite = m_pSClient->LastCount();
	if ( nWrite < requestSize )
	{
		wxLogDebug("%s: *** WARN *** Failed to send request [%s] ( %d < %d ). Error = [%s].", 
			FNAME, cmdRequest.c_str(), nWrite, requestSize, 
			hoxNetworkAPI::SocketErrorToString(m_pSClient->LastError()).c_str());
		return hoxRC_ERR;
	}

    return hoxRC_OK;
}

void
hoxChesscapeConnection::_DestroySocket()
{
    const char* FNAME = "hoxChesscapeConnection::_DestroySocket";

    if ( m_pSClient != NULL )
    {
        wxLogDebug("%s: Destroy the client socket...", FNAME);
        m_pSClient->Destroy();
        m_pSClient = NULL;
    }
    this->SetConnected( false );
}

hoxResult
hoxChesscapeConnection::_ReadLine( wxSocketBase* sock, 
                                   wxString&     result )
{
    const char* FNAME = "hoxChesscapeConnection::_ReadLine";
    wxString commandStr;

	/* Read a line between '0x02' and '0x03' */

	const wxChar START_CHAR = 0x02;
	const wxChar END_CHAR   = 0x03;
	bool   bStart = false;
    wxChar c;

    for (;;)
    {
        sock->Read( &c, 1 );
        if ( sock->LastCount() == 1 )
        {
			if ( !bStart && c == START_CHAR )
			{
				bStart = true;
			}
			else if ( bStart && c == END_CHAR )
			{
				result = commandStr;
				return hoxRC_OK;  // Done.
			}
            else
            {
                commandStr += c;

                // Impose some limit.
                if ( commandStr.size() >= hoxNETWORK_MAX_MSG_SIZE )
                {
                    wxLogError("%s: Maximum message's size [%d] reached. Likely to be an error.", 
                        FNAME, hoxNETWORK_MAX_MSG_SIZE);
                    wxLogError("%s: Partial read message (64 bytes) = [%s ...].", 
                        FNAME, commandStr.substr(0, 64).c_str());
                    break;
                }
            }
        }
        else if ( sock->Error() )
        {
            wxLogWarning("%s: Fail to read 1 byte from the network. Error = [%s].", 
                FNAME, hoxNetworkAPI::SocketErrorToString(sock->LastError()).c_str());
            wxLogWarning("%s: Result message accumulated so far = [%s].", FNAME, commandStr.c_str());
            break;
        }
    }

    return hoxRC_ERR;
}

/************************* END OF FILE ***************************************/
