/////////////////////////////////////////////////////////////////////////////
// Name:            hoxCoreBoard.cpp
// Program's Name:  Huy's Open Xiangqi
// Created:         10/05/2007
//
// Description:     The "core" Board.
/////////////////////////////////////////////////////////////////////////////

#include "hoxCoreBoard.h"
#include "hoxEnums.h"
#include "hoxPosition.h"
#include "hoxPiece.h"
#include "hoxUtility.h"
#include "hoxReferee.h"
#include "hoxTable.h"

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------

enum Constants
{
  // Dragging modes
  DRAG_MODE_NONE,
  DRAG_MODE_START,
  DRAG_MODE_DRAGGING,

  NUM_HORIZON_CELL  = 8,  // Do not change the value!!!
  NUM_VERTICAL_CELL = 9   // Do not change the value!!!
};

// ----------------------------------------------------------------------------
// hoxCoreBoard
// ----------------------------------------------------------------------------

IMPLEMENT_DYNAMIC_CLASS(hoxCoreBoard, wxPanel)

/*
 * The event-table.
 */
BEGIN_EVENT_TABLE(hoxCoreBoard, wxPanel)
    EVT_PAINT            (hoxCoreBoard::OnPaint)
    EVT_MOUSE_EVENTS     (hoxCoreBoard::OnMouseEvent)
    EVT_ERASE_BACKGROUND (hoxCoreBoard::OnEraseBackground)
    EVT_IDLE             (hoxCoreBoard::OnIdle)

    // NOTE: We have to handle this. Otherwise, the program crashes.
    //   @see http://www.nabble.com/EVT_MOUSE_CAPTURE_LOST-t2872494.html
    EVT_MOUSE_CAPTURE_LOST (hoxCoreBoard::OnMouseCaptureLost)
END_EVENT_TABLE()

hoxCoreBoard::hoxCoreBoard()
{
    wxFAIL_MSG( "This default constructor is never meant to be used." );
}

/**
 * NOTE: wx_FULL_REPAINT_ON_RESIZE is used to have entire window included 
 *       in the update region.
 */
hoxCoreBoard::hoxCoreBoard( wxWindow*      parent, 
                            hoxIReferee*   referee /* = new hoxNaiveReferee() */,
                            const wxPoint& pos  /* = wxDefaultPosition */, 
                            const wxSize&  size /* = wxDefaultSize*/ )
        : wxPanel( parent, wxID_ANY, 
                   pos, 
                   size,
                   wxFULL_REPAINT_ON_RESIZE )
        , m_referee( referee )
        , m_latestPiece( NULL )
{
    // NOTE: We move this PNG code since to outside to avoid
    //       having duplicate handles if there are more than
    //       one Board.
    //
    // Add PNG image-type handler since our pieces use this format.
    // wxImage::AddHandler( new wxPNGHandler );

    m_parent = parent;
    m_table = NULL;

    m_borderX = 40;
    m_borderY = m_borderX;
    m_bViewInverted = false;   // Normal view: RED is at bottom of the screen

    m_dragMode = DRAG_MODE_NONE;
    m_draggedPiece = NULL;
    m_dragImage = NULL;
}

hoxCoreBoard::~hoxCoreBoard()
{
    _ClearPieces();

    delete m_dragImage;

    // *** Let the Table take care the referee.
    // delete m_referee;
}

void 
hoxCoreBoard::OnPaint(wxPaintEvent &WXUNUSED(event))
{
    wxPaintDC dc(this);        // Device-Context
    PrepareDC(dc);   // prepare the device context for drawing a scrolled image
    //m_parent->PrepareDC(dc);

    _DoPaint(dc);
}

/**
 * Erase a given piece using THIS window's Device-Context.
 */
void 
hoxCoreBoard::_ErasePiece( hoxPiece* piece )
{
    wxClientDC dc(this);
    PrepareDC(dc);   // ... for drawing a scrolled image

    _ErasePieceWithDC( piece, dc );
}

/**
 * Erase a given piece using a specific Device-Context (DC).
 */
void 
hoxCoreBoard::_ErasePieceWithDC( hoxPiece* piece, 
                                 wxDC&     dc )
{
    /* Erase the piece by repainting the piece's area with
     * the Board's background.
     */

    wxRect rect( _GetPieceRect(piece) );
    dc.SetClippingRegion( rect );

    _DrawWorkSpace(dc);
    _DrawBoard(dc);

    dc.DestroyClippingRegion();
}

void 
hoxCoreBoard::_ClearPieces()
{
    hoxPieceList::const_iterator it;
    for (it = m_pieces.begin(); it != m_pieces.end(); ++it)
    {
        delete *it;
    }
}

// Find only active piece
hoxPiece* 
hoxCoreBoard::_FindPiece( const wxPoint& pt ) const
{
    hoxPieceList::const_iterator it;
    for (it = m_pieces.begin(); it != m_pieces.end(); ++it)
    {
        hoxPiece* piece = *it;
        if ( piece->IsActive() && _PieceHitTest(piece ,pt))
            return piece;
    }

    return NULL;
}

void 
hoxCoreBoard::_DoPaint( wxDC& dc )
{
    _DrawBoard(dc);  // Display board
    _DrawAllPieces(dc);   // Display pieces.
}

void 
hoxCoreBoard::_DrawBoard( wxDC& dc )
{
    wxSize totalSize = GetClientSize();   // of this Board

    dc.SetPen(*wxRED_PEN);
    dc.SetBrush( *wxLIGHT_GREY_BRUSH );

    // --- Get the board's max-dimension.
    wxCoord borderW = totalSize.GetWidth() - 2*m_borderX;
    wxCoord borderH = totalSize.GetHeight() - 2*m_borderY;

    // --- Calculate the cell's size (*** cell is a square ***)
    m_cellS = wxMin(borderW / NUM_HORIZON_CELL, borderH / NUM_VERTICAL_CELL);

    // --- Calculate the new "effective" board's size.
    wxCoord boardW = m_cellS * NUM_HORIZON_CELL;
    wxCoord boardH = m_cellS * NUM_VERTICAL_CELL;

    // *** Paint the board with a background's color.
    dc.SetBrush( *wxWHITE_BRUSH );
    dc.DrawRectangle( m_borderX, m_borderY, boardW, boardH );

    // Draw vertial lines.
    int line;
    wxCoord x1, y1, x2, y2;
    y1 = m_borderY;
    y2 = m_borderY + boardH;
    wxChar c = m_bViewInverted ? '8' : '0';
    for (line = 0; line < NUM_HORIZON_CELL+1; ++line)
    {
        x1 = m_borderX + line * m_cellS;
        x2 = x1;

        dc.DrawText(c, x1, y1-40);  // TODO  hard-coded for now
        dc.DrawLine(x1, y1, x2, y2);
        dc.DrawText(c, x2, y2+20);   // TODO  hard-coded for now
        if (m_bViewInverted) --c;
        else                 ++c;
    }

    // Draw horizontal lines.
    x1 = m_borderX;
    x2 = m_borderX + boardW;
    c = m_bViewInverted ? '9' : '0';
    for (line = 0; line < NUM_VERTICAL_CELL+1; ++line)
    {
        y1 = m_borderY + line * m_cellS;
        y2 = y1;

        dc.DrawText(c, x1-30, y1);  // TODO  hard-coded for now
        dc.DrawLine(x1, y1, x2, y2);
        dc.DrawText(c, x2+20, y2);  // TODO  hard-coded for now
        if (m_bViewInverted) --c;
        else                 ++c;
    }

    // Draw crossing lines at the red-palace.
    dc.DrawLine(m_borderX+3*m_cellS, m_borderY+9*m_cellS, 
    m_borderX+5*m_cellS, m_borderY+7*m_cellS);
    dc.DrawLine(m_borderX+3*m_cellS, m_borderY+7*m_cellS, 
    m_borderX+5*m_cellS, m_borderY+9*m_cellS);

    // Draw crossing lines at the black-palace.
    dc.DrawLine(m_borderX+3*m_cellS, m_borderY,
    m_borderX+5*m_cellS, m_borderY+2*m_cellS);
    dc.DrawLine(m_borderX+3*m_cellS, m_borderY+2*m_cellS, 
    m_borderX+5*m_cellS, m_borderY);

    // Delete lines at the 'river' by drawing lines with
    // the background's color.
    y1 = m_borderY + 4*m_cellS;
    y2 = y1 + m_cellS;
    dc.SetPen( *wxWHITE_PEN );
    for (line = 1; line < NUM_VERTICAL_CELL-1; ++line)
    {
        x1 = m_borderX + line * m_cellS;
        x2 = x1;

        dc.DrawLine(x1, y1, x2, y2);
    }
}

void hoxCoreBoard::_DrawWorkSpace( wxDC& dc )
{
    wxSize sz = GetClientSize();
    dc.SetBrush( *wxLIGHT_GREY_BRUSH );
    dc.DrawRectangle( 0, 0, sz.x, sz.y );;
}

void hoxCoreBoard::OnEraseBackground( wxEraseEvent& event )
{
    // *** Paint the entire working space with a background's color.
    if (event.GetDC())
    {
        _DrawWorkSpace(*event.GetDC());
    }
    else
    {
        wxClientDC dc(this);
        _DrawWorkSpace(dc);
    }
}

void hoxCoreBoard::OnIdle(wxIdleEvent& WXUNUSED(event))
{
    // Do nothing for now.
}

void
hoxCoreBoard::SetPiecesPath(const wxString& piecesPath)
{
    hoxUtility::SetPiecesPath( piecesPath );
}

void 
hoxCoreBoard::LoadPieces()
{
    wxASSERT_MSG( m_referee != NULL, _("The referee must have been set") );

    hoxPieceInfoList pieceInfoList;
    hoxPieceColor    nextColor;  // obtained but not used now!
    
    m_referee->GetGameState( pieceInfoList, nextColor );

    for ( hoxPieceInfoList::const_iterator it = pieceInfoList.begin();
                                           it != pieceInfoList.end(); 
                                         ++it )
    {
        hoxPiece* piece = new hoxPiece( *(*it) );
        m_pieces.push_back( piece );
    }

    hoxUtility::FreePieceInfoList( pieceInfoList );
}

void 
hoxCoreBoard::SetReferee( hoxIReferee* referee )
{
    wxASSERT( referee != NULL );
    m_referee = referee;
}

void 
hoxCoreBoard::SetTable( hoxTable* table )
{
    wxASSERT( table != NULL );
    m_table = table;
}

void 
hoxCoreBoard::_DrawAllPieces( wxDC& dc )
{
    for (hoxPieceList::const_iterator it = m_pieces.begin(); 
                                      it != m_pieces.end(); ++it)
    {
        hoxPiece* piece = *it;
        if ( piece->IsActive() && piece->IsShown()) 
        {
            _DrawPiece(dc, piece);
        }
    }
}

/**
 * Draw a given piece using THIS window's Device-Context.
 */
bool 
hoxCoreBoard::_DrawPiece( const hoxPiece* piece, 
                          int             op /* = wxCOPY */ )
{
    wxClientDC dc(this);
    PrepareDC(dc);   // ... for drawing a scrolled image

    return _DrawPiece( dc, piece, op );
}

bool 
hoxCoreBoard::_DrawPiece( wxDC&           dc, 
                          const hoxPiece* piece, 
                          int             op /* = wxCOPY */ )
{
    const wxPoint& pos = _GetPieceLocation( piece );
    const wxBitmap& bitmap = piece->GetBitmap();
    if ( ! bitmap.Ok() )
      return false;

    wxMemoryDC memDC;
    memDC.SelectObject( const_cast<wxBitmap&>(bitmap) );

    dc.Blit( pos.x, pos.y, 
             bitmap.GetWidth(), bitmap.GetHeight(),
             &memDC, 0, 0, op, true);

    /* Highlight the piece if it is the latest piece that moves. */
    if ( piece->IsLatest() )
    {
        int delta = 4;  // TODO: Hard-coded value.
        int x = pos.x + delta;
        int y = pos.y + delta;
        int w = bitmap.GetWidth() - 2*delta;
        int h = bitmap.GetHeight() - 2*delta;
        
        dc.SetPen(*wxCYAN);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle( wxRect(x, y, w, h) );
    }

    return true;
}

wxRect 
hoxCoreBoard::_GetPieceRect( const hoxPiece* piece ) const
{
    const wxPoint& pos = _GetPieceLocation(piece);
    const wxBitmap& bitmap = piece->GetBitmap();

    return wxRect( pos.x, pos.y, bitmap.GetWidth(), bitmap.GetHeight() ); 
}

bool 
hoxCoreBoard::_PieceHitTest( const hoxPiece* piece, 
                             const wxPoint&  pt ) const
{
    const wxRect& rect = _GetPieceRect(piece);
    return rect.Contains(pt.x, pt.y);
}

hoxPosition 
hoxCoreBoard::_PointToPosition( const hoxPiece* piece, 
                                const wxPoint&  p ) const
{
    const wxBitmap& bitmap = piece->GetBitmap();
    hoxPosition pos;

    // We will work on the center.
    wxPoint point(p.x + bitmap.GetWidth()/2, p.y + bitmap.GetHeight()/2);


    /* Get the 4 surrounding positions.
    *
    *    1 ------------ 2
    *    |      ^       |
    *    |      |       |
    *    |  <-- X -->   |
    *    |      |       |
    *    |      V       |
    *    4 ------------ 3
    *
    */

    wxPoint p1, p2, p3, p4;

    p1.x = point.x - ((point.x - m_borderX) % m_cellS);
    p1.y = point.y - ((point.y - m_borderY) % m_cellS);

    p2.x = p1.x + m_cellS; 
    p2.y = p1.y; 

    p3.x = p2.x; 
    p3.y = p2.y + m_cellS; 

    p4.x = p1.x; 
    p4.y = p3.y; 

    wxSize tolerance(m_cellS / 3, m_cellS / 3);
    wxRect  r1(p1, tolerance);
    wxRect  r2(wxPoint(p2.x - tolerance.GetWidth(), p2.y), tolerance);
    wxRect  r3(wxPoint(p3.x - tolerance.GetWidth(), p3.y - tolerance.GetHeight()), tolerance);
    wxRect  r4(wxPoint(p4.x, p4.y - tolerance.GetHeight()), tolerance);
  
    if ( r1.Contains(point) ) {
        pos.x = (p1.x - m_borderX) / m_cellS;
        pos.y = (p1.y - m_borderY) / m_cellS;
    }
    else if ( r2.Contains(point) ) {
        pos.x = (p2.x - m_borderX) / m_cellS;
        pos.y = (p2.y - m_borderY) / m_cellS;
    }
    else if ( r3.Contains(point) ) {
        pos.x = (p3.x - m_borderX) / m_cellS;
        pos.y = (p3.y - m_borderY) / m_cellS;
    }
    else if ( r4.Contains(point) ) {
        pos.x = (p4.x - m_borderX) / m_cellS;
        pos.y = (p4.y - m_borderY) / m_cellS;
    }

    return pos;
}

void
hoxCoreBoard::_MovePieceToPoint( hoxPiece*      piece, 
                                 const wxPoint& point )
{
    hoxPosition newPos = _PointToPosition(piece, point);

    // Convert to the 'real' position since we can be in the *inverted* view.
    if ( m_bViewInverted )
    {
        newPos.x = 8 - newPos.x;
        newPos.y = 9 - newPos.y;
    }

    // Call the "piece moved" handler.
    _OnPieceMoved(piece, newPos);
}

void 
hoxCoreBoard::_OnPieceMoved( hoxPiece*          piece, 
                             const hoxPosition& newPos )
{
    hoxMove move;   // Make a new Move

    /* If there is no referee, always assume the move is valid.
     * Otherwise, ask the referee to check if the move is valid.
     */

    if ( m_referee != NULL )
    {
        move.piece       = piece->GetInfo();
        move.newPosition = newPos;

        if ( ! m_referee->ValidateMove( move ) )
        {
            wxLogWarning("Move is not valid!!!");
            this->Refresh();
            return;
        }
    }

    /* NOTE: Need to the following check. 
     * Otherwise, the piece would disappear.
     */
    if ( piece->GetPosition() != newPos )
    {
        MovePieceToPosition(piece, newPos);
    }

    /* Inform the board's owner of the new move. */
    if ( m_table != NULL )
    {
        m_table->OnMove_FromBoard( move );
    }
}

/**
 * Set a piece's position without validation 
 * (without going through the referee).
 */
bool 
hoxCoreBoard::MovePieceToPosition( hoxPiece*          piece, 
                                   const hoxPosition& newPosition )
{
    // Sanity check.
    if ( ! newPosition.IsValid() )
        return false;

    // Remove captured piece, if any.
    hoxPiece* pCapturedPiece = GetPieceAt( newPosition );
    if ( pCapturedPiece != NULL )
    {
        _ErasePiece( pCapturedPiece );
        pCapturedPiece->SetActive(false);
    }

    // Un-highlight the "old" piece.
    if ( m_latestPiece != NULL )
    {
        m_latestPiece->SetLatest(false);

        /* Re-draw the "old" piece to undo the highlight.
         * TODO: Is there a better way?
         */
        _ErasePiece( m_latestPiece );
        _DrawPiece( m_latestPiece );
    }

    // Clear the old image if it was visible.
    if ( piece->IsShown() )
        _ErasePiece( piece );

    // Simply set the position without validation.
    bool bRet = piece->SetPosition( newPosition );

    m_latestPiece = piece;
    m_latestPiece->SetLatest( true );

    _DrawPiece( piece );

    return bRet;
}

void 
hoxCoreBoard::OnMouseEvent( wxMouseEvent& event )
{
    if ( event.LeftDown() )
    {
        hoxPiece* piece = _FindPiece(event.GetPosition());
        if ( piece != NULL )
        {
            // We tentatively start dragging, but wait for
            // mouse movement before dragging properly.

            m_dragMode     = DRAG_MODE_START;
            m_dragStartPos = event.GetPosition();
            m_draggedPiece = piece;
        }
    }
    else if (event.Dragging() && m_dragMode == DRAG_MODE_START)
    {
        // We will start dragging if we've moved beyond a couple of pixels

        int tolerance = 2;  // NOTE: Hard-coded value.
        int dx = abs(event.GetPosition().x - m_dragStartPos.x);
        int dy = abs(event.GetPosition().y - m_dragStartPos.y);
        if (dx <= tolerance && dy <= tolerance) {
            return;
        }

        // Start the drag.
        m_dragMode = DRAG_MODE_DRAGGING;

        delete m_dragImage;

        // Erase the dragged shape from the board
        m_draggedPiece->SetShow(false);
        _ErasePiece(m_draggedPiece);

        m_dragImage = new wxDragImage( m_draggedPiece->GetBitmap(), 
                                       wxCursor(wxCURSOR_HAND) );

        // The offset between the top-left of the shape image and the current shape position
        wxPoint beginDragHotSpot = m_dragStartPos - _GetPieceLocation(m_draggedPiece);

        // Now we do this inside the implementation: always assume
        // coordinates relative to the capture window (client coordinates)

        // Initiate the Drag.
        if ( m_dragImage->BeginDrag( beginDragHotSpot, this, 
                                     false /* only within this window */))
        {
            m_dragImage->Move(event.GetPosition());
            m_dragImage->Show();
        } 
        else  // Drag fails --> Cancel the Drag
        {
            delete m_dragImage;
            m_dragImage = NULL;
            m_dragMode = DRAG_MODE_NONE;
        }
    }
    else if (event.Dragging() && m_dragMode == DRAG_MODE_DRAGGING)
    {
        m_dragImage->Move(event.GetPosition());
    }
    else if (event.LeftUp() && m_dragMode != DRAG_MODE_NONE)
    {
        // Finish dragging

        m_dragMode = DRAG_MODE_NONE;

        if (!m_draggedPiece || !m_dragImage) {
            return;
        }

        // Hide the temporary Drag image.
        m_dragImage->Hide();
        m_dragImage->EndDrag();
        delete m_dragImage;
        m_dragImage = NULL;

        // Move the dragged piece to its new location.
        wxPoint newPoint = _GetPieceLocation(m_draggedPiece) 
                         + event.GetPosition() - m_dragStartPos;
        m_draggedPiece->SetShow(true);
        _MovePieceToPoint( m_draggedPiece, newPoint );
        m_draggedPiece = NULL;
    }
}

void 
hoxCoreBoard::OnMouseCaptureLost( wxMouseCaptureLostEvent& event )
{
    wxLogWarning("**** Receive MOUSE_CAPTURE_LOST event ****");
}

//
// Return the top-left Point of a piece.
//
wxPoint 
hoxCoreBoard::_GetPieceLocation( const hoxPiece* piece ) const
{
    hoxPosition pos = piece->GetPosition();

    // Convert to the 'real' position since we can be in the *inverted* view.
    if (m_bViewInverted)
    {
        pos.x = 8 - pos.x;
        pos.y = 9 - pos.y;
    }

    // Determine the center of the piece.
    int posX = m_borderX + pos.x * m_cellS;
    int posY = m_borderY + pos.y * m_cellS;

    // Return the top-left point of the piece.
    const wxBitmap& bitmap = piece->GetBitmap();
    posX -= bitmap.GetWidth()/2;
    posY -= bitmap.GetHeight()/2;

    return wxPoint( posX, posY );
}

//
// Return 'NULL' if no ** active ** piece is at the specified position.
//
hoxPiece* 
hoxCoreBoard::GetPieceAt( const hoxPosition& pos ) const
{
    hoxPieceList::const_iterator it;
    for (it = m_pieces.begin(); it != m_pieces.end(); ++it)
    {
        hoxPiece* piece = *it;
        if ( piece->IsActive() && piece->GetPosition() == pos) 
        {
            return piece;
        }
    }

    return NULL;
}

// toggle view side: Red/Black is at the bottom.
void 
hoxCoreBoard::ToggleViewSide()
{
    m_bViewInverted = !m_bViewInverted;

    wxClientDC dc(this);
    _DrawWorkSpace(dc);
    _DrawBoard(dc);
    _DrawAllPieces(dc);
}

wxSize 
hoxCoreBoard::GetBestBoardSize( const int proposedHeight ) const
{
    const char* FNAME = "hoxCoreBoard::GetBestBoardSize";
    wxSize totalSize( proposedHeight, proposedHeight );

    // --- Get the board's max-dimension.
    wxCoord borderW = totalSize.GetWidth() - 2*m_borderX;
    wxCoord borderH = totalSize.GetHeight() - 2*m_borderY;

    wxCoord cellS = wxMin(borderW / NUM_HORIZON_CELL, borderH / NUM_VERTICAL_CELL);

    // --- Calculate the new "effective" board's size.
    wxCoord boardW = cellS * NUM_HORIZON_CELL;
    wxCoord boardH = cellS * NUM_VERTICAL_CELL;

    int wholeBoardX = boardW + 2*m_borderX;
    int wholeBoardY = boardH + 2*m_borderY;

    wxSize bestSize( wholeBoardX, wholeBoardY );

    //wxLogDebug("%s: (%d x %d) --> (%d x %d)", FNAME,
    //    totalSize.GetWidth(), totalSize.GetHeight(),
    //    bestSize.GetWidth(), bestSize.GetHeight());
    return bestSize;
}

wxSize 
hoxCoreBoard::DoGetBestSize() const
{
    const char* FNAME = "hoxCoreBoard::DoGetBestSize";

    wxSize availableSize = GetParent()->GetClientSize();    

    int proposedHeight = 
        availableSize.GetHeight() - 50 /* TODO: The two players' info */;

    wxSize bestSize = this->GetBestBoardSize( proposedHeight );

    //wxLogDebug("%s: (%d) --> (%d x %d)", FNAME,
    //    proposedHeight,
    //    bestSize.GetWidth(), bestSize.GetHeight());
    return bestSize;
}

/************************* END OF FILE ***************************************/