/**************************************************************
 * 
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 * 
 *************************************************************/



// MARKER(update_precomp.py): autogen include statement, do not remove
#include "precompiled_sd.hxx"

#include "OutlineView.hxx"
#include <memory>
#include <editeng/forbiddencharacterstable.hxx>
#include <sfx2/progress.hxx>
#include <vcl/wrkwin.hxx>
#include <svx/svxids.hrc>
#include "eetext.hxx"
#include <editeng/eeitem.hxx>
#include <editeng/editstat.hxx>
#include <editeng/lrspitem.hxx>
#include <svx/svdotext.hxx>
#include <sfx2/printer.hxx>
#include <sfx2/imagemgr.hxx>
#include <sfx2/app.hxx>
#include <sfx2/bindings.hxx>
#include <svl/itempool.hxx>
#include <svl/style.hxx>
#include <svx/svdorect.hxx>
#include <svx/svdundo.hxx>
#include <svl/brdcst.hxx>
#include <vcl/msgbox.hxx>
#include <editeng/adjitem.hxx>
#include <editeng/tstpitem.hxx>
#include <editeng/lspcitem.hxx>
#include <editeng/numitem.hxx>
#include <editeng/outlobj.hxx>
#include <editeng/numitem.hxx>
#include <editeng/editeng.hxx>

// #97766#
#include <editeng/editobj.hxx>
#include <editeng/editund2.hxx>

#include <editeng/editview.hxx>
#include <editeng/svxfont.hxx>
#include <editeng/fhgtitem.hxx>

#include "DrawDocShell.hxx"
#include "drawdoc.hxx"
#include "Window.hxx"
#include "sdpage.hxx"
#include "pres.hxx"
#include "OutlineViewShell.hxx"
#include "app.hrc"
#include "glob.hrc"
#include "sdresid.hxx"
#include "Outliner.hxx"
#include "strings.hrc"
#include "EventMultiplexer.hxx"
#include "ViewShellBase.hxx"
#include "undo/undoobjects.hxx"
#include "undo/undomanager.hxx"
#include "stlsheet.hxx"

using ::rtl::OUString;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::frame;

namespace sd {

// Breite: DIN A 4,  zwei Raender zu je 1 cm
#define OUTLINE_PAPERWIDTH 19000

// beim Seitenmanipulation Fortschrittsanzeige, wenn mehr Seiten betroffen
// sind als:
#define PROCESS_WITH_PROGRESS_THRESHOLD  5

struct SdParaAndPos
{
	Paragraph* pPara;
	sal_uInt16	   nPos;
};

TYPEINIT1( OutlineView, ::sd::View );

/*************************************************************************
|*
|* Konstruktor
|*
\************************************************************************/

OutlineView::OutlineView( DrawDocShell* pDocSh, ::Window* pWindow, OutlineViewShell* pOutlineViewSh)
: ::sd::View(pDocSh->GetDoc(), pWindow, pOutlineViewSh)
, mpOutlineViewShell(pOutlineViewSh)
, mpOutliner( mpDoc->GetOutliner(sal_True) )
, mpOldParaOrder(NULL)
, mpSelectedParas(NULL)
, mnPagesToProcess(0)
, mnPagesProcessed(0)
, mbFirstPaint(sal_True)
, mpProgress(NULL)
, mbHighContrastMode( false )
, maDocColor( COL_WHITE )
, mnPageNumberWidthPixel( 0 )
, maLRSpaceItem( 0, 0, 2000, 0, EE_PARA_OUTLLRSPACE )
{
	sal_Bool bInitOutliner = sal_False;

	if (mpOutliner->GetViewCount() == 0)
	{
		// Outliner initialisieren: Referenz-Device setzen
		bInitOutliner = sal_True;
		mpOutliner->Init( OUTLINERMODE_OUTLINEVIEW );
/*
		SfxStyleSheet* pTitleSheet = mpDoc->GetSdPage( 0, PK_STANDARD )->GetStyleSheetForPresObj( PRESOBJ_TITLE );

		if ( pTitleSheet )
		{
			// set title symbol (level 0)
			SvxNumBulletItem aNumBulletItem( (const SvxNumBulletItem&) pTitleSheet->GetItemSet().Get(EE_PARA_NUMBULLET) );
			SvxNumRule aNumRule(* aNumBulletItem.GetNumRule());
			SvxNumberFormat aFormat( aNumRule.GetLevel(0));
            Font    aBulletFont;
            const Font* pFont = aFormat.GetBulletFont();
            if ( pFont )                                        // if available take font size and color from style
                aBulletFont = *pFont;
            else
            {
                aBulletFont.SetColor( COL_AUTO );
                aBulletFont.SetHeight( 1552 );
            }
            aBulletFont.SetCharSet(RTL_TEXTENCODING_MS_1252);   // and replacing other values by standard
            aBulletFont.SetName( String( RTL_CONSTASCII_USTRINGPARAM( "StarSymbol" )) );
	        aBulletFont.SetWeight(WEIGHT_NORMAL);
	        aBulletFont.SetUnderline(UNDERLINE_NONE);
	        aBulletFont.SetStrikeout(STRIKEOUT_NONE);
	        aBulletFont.SetItalic(ITALIC_NONE);
	        aBulletFont.SetOutline(sal_False);
	        aBulletFont.SetShadow(sal_False);
            aFormat.SetBulletFont( &aBulletFont );
			aFormat.SetBulletChar( 0xE011 );  // StarBats: 0xF000 + 114
			mpOutliner->OverwriteLevel0Bullet( aFormat );
		}
*/
		mpOutliner->SetRefDevice( SD_MOD()->GetRefDevice( *pDocSh ) );
		sal_uLong nWidth = OUTLINE_PAPERWIDTH;
		mpOutliner->SetPaperSize(Size(nWidth, 400000000));
	}

	// View in Outliner einfuegen
	for (sal_uInt16 nView = 0; nView < MAX_OUTLINERVIEWS; nView++)
	{
		mpOutlinerView[nView] = NULL;
	}

	mpOutlinerView[0] = new OutlinerView(mpOutliner, pWindow);
	Rectangle aNullRect;
	mpOutlinerView[0]->SetOutputArea(aNullRect);
	mpOutliner->SetUpdateMode(sal_False);
	mpOutliner->InsertView(mpOutlinerView[0], LIST_APPEND);

	onUpdateStyleSettings( true );

	if (bInitOutliner)
	{
		// Outliner mit Inhalt fuellen
		FillOutliner();
	}

	Link aLink( LINK(this,OutlineView,EventMultiplexerListener) );
    mpOutlineViewShell->GetViewShellBase().GetEventMultiplexer()->AddEventListener(
        aLink,
        tools::EventMultiplexerEvent::EID_CURRENT_PAGE
        | tools::EventMultiplexerEvent::EID_PAGE_ORDER);

	LanguageType eLang = mpOutliner->GetDefaultLanguage();
	maPageNumberFont = OutputDevice::GetDefaultFont( DEFAULTFONT_SANS_UNICODE, eLang, 0 );
	maPageNumberFont.SetHeight( 500 );

	maBulletFont.SetColor( COL_AUTO );
	maBulletFont.SetHeight( 1000 );
	maBulletFont.SetCharSet(RTL_TEXTENCODING_MS_1252);   // and replacing other values by standard
    maBulletFont.SetName( String( RTL_CONSTASCII_USTRINGPARAM( "StarSymbol" )) );
    maBulletFont.SetWeight(WEIGHT_NORMAL);
    maBulletFont.SetUnderline(UNDERLINE_NONE);
    maBulletFont.SetStrikeout(STRIKEOUT_NONE);
    maBulletFont.SetItalic(ITALIC_NONE);
    maBulletFont.SetOutline(sal_False);
    maBulletFont.SetShadow(sal_False);


	Reference<XFrame> xFrame (mpOutlineViewShell->GetViewShellBase().GetFrame()->GetTopFrame().GetFrameInterface(), UNO_QUERY);

    const OUString aSlotURL( RTL_CONSTASCII_USTRINGPARAM( ".uno:ShowSlide" ));
    maSlideImage = GetImage( xFrame, aSlotURL, true, false /* todo, hc mode */ );

    // Tell undo manager of the document about the undo manager of the
    // outliner, so that the former can synchronize with the later.
    sd::UndoManager* pDocUndoMgr = dynamic_cast<sd::UndoManager*>(mpDocSh->GetUndoManager());
    if (pDocUndoMgr != NULL)
        pDocUndoMgr->SetLinkedUndoManager(&mpOutliner->GetUndoManager());
}

/*************************************************************************
|*
|* Destruktor, Links restaurieren, Outliner leeren
|*
\************************************************************************/

OutlineView::~OutlineView()
{
	DBG_ASSERT(maDragAndDropModelGuard.get() == 0, "sd::OutlineView::~OutlineView(), prior drag operation not finished correctly!" );

	Link aLink( LINK(this,OutlineView,EventMultiplexerListener) );
    mpOutlineViewShell->GetViewShellBase().GetEventMultiplexer()->RemoveEventListener( aLink );
	DisconnectFromApplication();

	if( mpProgress )
		delete mpProgress;

	// OutlinerViews abmelden und zerstoeren
	for (sal_uInt16 nView = 0; nView < MAX_OUTLINERVIEWS; nView++)
	{
		if (mpOutlinerView[nView] != NULL)
		{
			mpOutliner->RemoveView( mpOutlinerView[nView] );
			delete mpOutlinerView[nView];
			mpOutlinerView[nView] = NULL;
		}
	}

	if (mpOutliner->GetViewCount() == 0)
	{
		// Outliner deinitialisieren: Farbdarstellung einschalten
		ResetLinks();
		sal_uLong nCntrl = mpOutliner->GetControlWord();
		mpOutliner->SetUpdateMode(sal_False); // sonst wird bei SetControlWord gezeichnet
		mpOutliner->SetControlWord(nCntrl & ~EE_CNTRL_NOCOLORS);
		SvtAccessibilityOptions aOptions;
		mpOutliner->ForceAutoColor( aOptions.GetIsAutomaticFontColor() );
		mpOutliner->Clear();
	}

	DBG_ASSERT(!mpSelectedParas, "Absatzliste nicht geloescht");
	DBG_ASSERT(!mpOldParaOrder, "Absatzliste nicht geloescht");
}




void OutlineView::ConnectToApplication (void)
{
	mpOutlineViewShell->GetActiveWindow()->GrabFocus();
	Application::AddEventListener(LINK(this, OutlineView, AppEventListenerHdl));
}




void OutlineView::DisconnectFromApplication (void)
{
	Application::RemoveEventListener(LINK(this, OutlineView, AppEventListenerHdl));
}




/*************************************************************************
|*
|* Paint-Methode
|*
\************************************************************************/

void OutlineView::Paint(const Rectangle& rRect, ::sd::Window* pWin)
{
	OutlinerView* pOlView = GetViewByWindow(pWin);

	if (pOlView)
	{
		pOlView->HideCursor();
		pOlView->Paint(rRect);

		pOlView->ShowCursor(mbFirstPaint);

/*
		if( mnPageNumberWidthPixel == 0 )
			GetPageNumberWidthPixel();

		const sal_uLong nParaCount = pOlView->GetOutliner()->GetParagraphCount();
		EditView& rEditView = pOlView->GetEditView();

		Font aOldFont( pWin->GetFont() );

		const String aBulletStr( sal_Unicode( 0xE011 ) );
		pWin->SetFont( maBulletFont);
		sal_Int32 nBulletWidth = pWin->GetTextWidth(aBulletStr);

		sal_Int32 nPage = 1;
		for( sal_uLong nPara = 0; nPara < nParaCount; nPara++ )
		{
			Paragraph* pPara = pOlView->GetOutliner()->GetParagraph( nPara );
			if( pPara->HasFlag( PARAFLAG_ISPAGE ) )
			{
				pWin->SetFont( maPageNumberFont );
				const String aStr( String::CreateFromInt32( nPage++ ) );
				Point aPos( rEditView.GetWindowPosTopLeft( (sal_uInt16)nPara ) );

				sal_Int32 nNumberOffset = pWin->PixelToLogic( Point(mnPageNumberWidthPixel, 0) ).X() - nBulletWidth;
				sal_Int32 nLineHeight = pOlView->GetOutliner()->GetLineHeight( nPara, 0 );

				aPos.X() = nNumberOffset;
				
				Point aPoint( aPos.X() - pWin->GetTextWidth( aStr ), aPos.Y() + ( nLineHeight - maPageNumberFont.GetHeight()) / 2 );
				pWin->DrawText( aPoint, aStr );

				aPoint.X() = aPos.X();
				aPoint.Y() = aPos.Y() +( nLineHeight - maBulletFont.GetHeight()) / 2;
				pWin->SetFont( maBulletFont );
				pWin->DrawText( aPoint, aBulletStr );
			}
		}

		pWin->SetFont( aOldFont );
*/		
		mbFirstPaint = sal_False;
	}
}

void OutlineView::InvalidateSlideNumberArea()
{   
/*
	for( sal_Int16 nView = 0; nView < MAX_OUTLINERVIEWS; ++nView )
	{
		if (mpOutlinerView[nView] != NULL)
		{
            ::Window* pWindow = mpOutlinerView[nView]->GetWindow();
            if( pWindow )
            {
                Rectangle aRect( Point(0,0), pWindow->GetOutputSize() );
                aRect.nRight = aRect.nLeft + pWindow->PixelToLogic( Point( mnPageNumberWidthPixel, 0 ) ).X() * 2;

                pWindow->Invalidate(aRect);
            }
        }
    }
*/
}

/*************************************************************************
|*
|* Fenster-Groesse hat sich geaendert
|*
\************************************************************************/

void OutlineView::AdjustPosSizePixel(const Point &,const Size &,::sd::Window*)
{
}

/*************************************************************************
|*
|* ein Fenster hinzufuegen
|*
\************************************************************************/

void OutlineView::AddWindowToPaintView(OutputDevice* pWin)
{
	sal_Bool bAdded = sal_False;
	sal_Bool bValidArea = sal_False;
	Rectangle aOutputArea;
	const Color aWhiteColor( COL_WHITE );
	sal_uInt16 nView = 0;

	while (nView < MAX_OUTLINERVIEWS && !bAdded)
	{
		if (mpOutlinerView[nView] == NULL)
		{
			mpOutlinerView[nView] = new OutlinerView(mpOutliner, dynamic_cast< ::sd::Window* >(pWin));
			mpOutlinerView[nView]->SetBackgroundColor( aWhiteColor );
			mpOutliner->InsertView(mpOutlinerView[nView], LIST_APPEND);
			bAdded = sal_True;

			if (bValidArea)
			{
				mpOutlinerView[nView]->SetOutputArea(aOutputArea);
			}
		}
		else if (!bValidArea)
		{
			aOutputArea = mpOutlinerView[nView]->GetOutputArea();
			bValidArea = sal_True;
		}

		nView++;
	}

	// weisser Hintergrund im Outliner
	pWin->SetBackground( Wallpaper( aWhiteColor ) );

	::sd::View::AddWindowToPaintView(pWin);
}

/*************************************************************************
|*
|* ein Fenster entfernen
|*
\************************************************************************/

void OutlineView::DeleteWindowFromPaintView(OutputDevice* pWin)
{
	sal_Bool bRemoved = sal_False;
	sal_uInt16 nView = 0;
	::Window* pWindow;

	while (nView < MAX_OUTLINERVIEWS && !bRemoved)
	{
		if (mpOutlinerView[nView] != NULL)
		{
			pWindow = mpOutlinerView[nView]->GetWindow();

			if (pWindow == pWin)
			{
				mpOutliner->RemoveView( mpOutlinerView[nView] );
				delete mpOutlinerView[nView];
				mpOutlinerView[nView] = NULL;
				bRemoved = sal_True;
			}
		}

		nView++;
	}

	::sd::View::DeleteWindowFromPaintView(pWin);
}

/*************************************************************************
|*
|* Zeiger der dem Fenster entsprechenden OutlinerView zurueckgeben.
|*
\************************************************************************/

OutlinerView* OutlineView::GetViewByWindow (::Window* pWin) const
{
	OutlinerView* pOlView = NULL;
	for (sal_uInt16 nView = 0; nView < MAX_OUTLINERVIEWS; nView++)
	{
		if (mpOutlinerView[nView] != NULL)
		{
			if ( pWin == mpOutlinerView[nView]->GetWindow() )
			{
				pOlView = mpOutlinerView[nView];
			}
		}
	}
	return (pOlView);
}


/*************************************************************************
|*
|* Ermittelt den Titel vor einem beliebigen Absatz.
|*
\************************************************************************/

Paragraph* OutlineView::GetPrevTitle(const Paragraph* pPara)
{
	sal_Int32 nPos = mpOutliner->GetAbsPos(const_cast<Paragraph*>(pPara));

	if (nPos > 0)
	{
		while(nPos)
		{
			pPara = mpOutliner->GetParagraph(--nPos);
			if( mpOutliner->HasParaFlag(pPara, PARAFLAG_ISPAGE) )
			{
				return const_cast< Paragraph* >( pPara );
			}
		}

	}
	return NULL;
}

/*************************************************************************
|*
|* Ermittelt den Titel nach einem beliebigen Absatz.
|*
\************************************************************************/

Paragraph* OutlineView::GetNextTitle(const Paragraph* pPara)
{
	Paragraph* pResult = const_cast< Paragraph* >( pPara );

	sal_Int32 nPos = mpOutliner->GetAbsPos(pResult);

	do
	{
		pResult = mpOutliner->GetParagraph(++nPos);
		if( pResult && mpOutliner->HasParaFlag(pResult, PARAFLAG_ISPAGE) )
			return pResult;
	}
	while( pResult );

	return NULL;
}

/*************************************************************************
|*
|* Handler fuer das Einfuegen von Seiten (Absaetzen)
|*
\************************************************************************/

IMPL_LINK( OutlineView, ParagraphInsertedHdl, ::Outliner *, pOutliner )
{
//	DBG_ASSERT( isRecordingUndo(), "sd::OutlineView::ParagraphInsertedHdl(), model change without undo?!" );

	// we get calls to this handler during binary insert of drag and drop contents but
	// we ignore it here and handle it later in OnEndPasteOrDrop()
	if( maDragAndDropModelGuard.get() == 0 )
	{
		OutlineViewPageChangesGuard aGuard(this);

		Paragraph* pPara = pOutliner->GetHdlParagraph();

		sal_uInt16 nAbsPos = (sal_uInt16)mpOutliner->GetAbsPos( pPara );

		UpdateParagraph( nAbsPos );

		if( (nAbsPos == 0) || mpOutliner->HasParaFlag(pPara,PARAFLAG_ISPAGE) || mpOutliner->HasParaFlag(mpOutliner->GetParagraph( nAbsPos-1 ), PARAFLAG_ISPAGE) )
		{
			InsertSlideForParagraph( pPara );
			InvalidateSlideNumberArea();
		}
	}

	return 0;
}

/** creates and inserts an empty slide for the given paragraph */
SdPage* OutlineView::InsertSlideForParagraph( Paragraph* pPara )
{
	DBG_ASSERT( isRecordingUndo(), "sd::OutlineView::InsertSlideForParagraph(), model change without undo?!" );

	OutlineViewPageChangesGuard aGuard(this);

	mpOutliner->SetParaFlag( pPara, PARAFLAG_ISPAGE );
	// wieviele Titel sind vor dem neuen Titelabsatz?
	sal_uLong nExample = 0L;			// Position der "Vorbild"seite
	sal_uLong nTarget  = 0L;			// Einfuegeposition
	while(pPara)
	{
		pPara = GetPrevTitle(pPara);
		if (pPara)
			nTarget++;
	}


	// was der Outliner nicht kann, muss hier wieder wettgemacht werden:
	// wenn VOR dem ersten Absatz ein neuer Absatz mit RETURN erzeugt wird,
	// meldet der Outliner den bereits bestehenden (jetzt nach unten
	// gerutschten) Absatz als neuen Absatz; nicht darauf reinfallen!
	if (nTarget == 1)
	{
		String aTest(mpOutliner->GetText( mpOutliner->GetParagraph( 0 ) ));
		if (aTest.Len() == 0)
		{
			nTarget = 0;
		}
	}


	// "Vorbild"seite ist - wenn vorhanden - die Vorgaengerseite
	if (nTarget > 0)
	{
		nExample = nTarget - 1;

		sal_uInt16 nPageCount = mpDoc->GetSdPageCount( PK_STANDARD );
		if( nExample >= nPageCount )
			nExample = nPageCount - 1;
	}

	/**********************************************************************
	* Es wird stets zuerst eine Standardseite und dann eine
	* Notizseite erzeugt. Es ist sichergestellt, dass auf eine
	* Standardseite stets die zugehoerige Notizseite folgt.
	* Vorangestellt ist genau eine Handzettelseite
	**********************************************************************/

	// diese Seite hat Vorbildfunktion
	SdPage* pExample = (SdPage*)mpDoc->GetSdPage((sal_uInt16)nExample, PK_STANDARD);
	SdPage* pPage = (SdPage*)mpDoc->AllocPage(sal_False);

	pPage->SetLayoutName(pExample->GetLayoutName());

	// einfuegen (Seite)
	mpDoc->InsertPage(pPage, (sal_uInt16)(nTarget) * 2 + 1);
	if( isRecordingUndo() )
		AddUndo(mpDoc->GetSdrUndoFactory().CreateUndoNewPage(*pPage));

	// der Standardseite eine Masterpage zuweisen
	pPage->TRG_SetMasterPage(pExample->TRG_GetMasterPage());

	// Seitengroesse setzen
	pPage->SetSize(pExample->GetSize());
	pPage->SetBorder( pExample->GetLftBorder(),
					  pExample->GetUppBorder(),
					  pExample->GetRgtBorder(),
					  pExample->GetLwrBorder() );

	// neue Praesentationsobjekte anlegen (auf <Titel> oder
	// <Titel mit Untertitel> folgt <Titel mit Gliederung>, ansonsten
	// wird das Layout von der Vorgaengerseite uebernommen)
	AutoLayout eAutoLayout = pExample->GetAutoLayout();
	if (eAutoLayout == AUTOLAYOUT_TITLE ||
		eAutoLayout == AUTOLAYOUT_ONLY_TITLE)
	{
		pPage->SetAutoLayout(AUTOLAYOUT_ENUM, sal_True);
	}
	else
	{
		pPage->SetAutoLayout(pExample->GetAutoLayout(), sal_True);
	}

	/**********************************************************************
	|* jetzt die Notizseite
	\*********************************************************************/
	pExample = (SdPage*)mpDoc->GetSdPage((sal_uInt16)nExample, PK_NOTES);
	SdPage* pNotesPage = (SdPage*)mpDoc->AllocPage(sal_False);

	pNotesPage->SetLayoutName(pExample->GetLayoutName());

	pNotesPage->SetPageKind(PK_NOTES);

	// einfuegen (Notizseite)
	mpDoc->InsertPage(pNotesPage, (sal_uInt16)(nTarget) * 2 + 2);
	if( isRecordingUndo() )
		AddUndo(mpDoc->GetSdrUndoFactory().CreateUndoNewPage(*pNotesPage));

	// der Notizseite eine Masterpage zuweisen
	pNotesPage->TRG_SetMasterPage(pExample->TRG_GetMasterPage());

	// Seitengroesse setzen, es muss bereits eine Seite vorhanden sein
	pNotesPage->SetSize(pExample->GetSize());
	pNotesPage->SetBorder( pExample->GetLftBorder(),
						   pExample->GetUppBorder(),
						   pExample->GetRgtBorder(),
						   pExample->GetLwrBorder() );

	// neue Praesentationsobjekte anlegen
	pNotesPage->SetAutoLayout(pExample->GetAutoLayout(), sal_True);

	mpOutliner->UpdateFields();

	return pPage;
}

/*************************************************************************
|*
|* Handler fuer das Loeschen von Seiten (Absaetzen)
|*
\************************************************************************/

IMPL_LINK( OutlineView, ParagraphRemovingHdl, ::Outliner *, pOutliner )
{
	DBG_ASSERT( isRecordingUndo(), "sd::OutlineView::ParagraphRemovingHdl(), model change without undo?!" );

	OutlineViewPageChangesGuard aGuard(this);

	Paragraph* pPara = pOutliner->GetHdlParagraph();
	if( pOutliner->HasParaFlag( pPara, PARAFLAG_ISPAGE ) )
	{
		// wieviele Titel sind vor dem fraglichen Titelabsatz?
		sal_uLong nPos = 0L;
		while(pPara)
		{
			pPara = GetPrevTitle(pPara);
			if (pPara) nPos++;
		}

		// Seite und Notizseite loeschen
		sal_uInt16 nAbsPos = (sal_uInt16)nPos * 2 + 1;
		SdrPage* pPage = mpDoc->GetPage(nAbsPos);
		if( isRecordingUndo() )
			AddUndo(mpDoc->GetSdrUndoFactory().CreateUndoDeletePage(*pPage));
		mpDoc->RemovePage(nAbsPos);

		nAbsPos = (sal_uInt16)nPos * 2 + 1;
		pPage = mpDoc->GetPage(nAbsPos);
		if( isRecordingUndo() )
			AddUndo(mpDoc->GetSdrUndoFactory().CreateUndoDeletePage(*pPage));
		mpDoc->RemovePage(nAbsPos);

		// ggfs. Fortschrittsanzeige
		if (mnPagesToProcess)
		{
			mnPagesProcessed++;

			if(mpProgress)
				mpProgress->SetState(mnPagesProcessed);

			if (mnPagesProcessed == mnPagesToProcess)
			{
				if(mpProgress)
				{
					delete mpProgress;
					mpProgress = NULL;
				}
				mnPagesToProcess = 0;
				mnPagesProcessed = 0;
			}
		}
		pOutliner->UpdateFields();
	}

    InvalidateSlideNumberArea();

	return 0;
}

/*************************************************************************
|*
|* Handler fuer das Aendern der Einruecktiefe von Absaetzen (macht ggfs.
|* das Einfuegen oder Loeschen von Seiten notwendig)
|*
\************************************************************************/

IMPL_LINK( OutlineView, DepthChangedHdl, ::Outliner *, pOutliner )
{
	DBG_ASSERT( isRecordingUndo(), "sd::OutlineView::DepthChangedHdl(), no undo for model change?!" );

	OutlineViewPageChangesGuard aGuard(this);

	Paragraph* pPara = pOutliner->GetHdlParagraph();
	if( pOutliner->HasParaFlag( pPara, PARAFLAG_ISPAGE ) && ((pOutliner->GetPrevFlags() & PARAFLAG_ISPAGE) == 0) )
	{
		// the current paragraph is transformed into a slide

		mpOutliner->SetDepth( pPara, -1 );

		// werden da etwa mehrere Level-1-Absaetze auf Level 0 gebracht und
		// wir sollten eine Fortschrittsanzeige oder Eieruhr aufsetzen und
		// haben es noch nicht getan?
		if (mnPagesToProcess == 0)
		{
			Window* 	  pActWin = mpOutlineViewShell->GetActiveWindow();
			OutlinerView* pOlView = GetViewByWindow(pActWin);
			List*		  pList   = pOlView->CreateSelectionList();

			Paragraph*	  pParagraph   = (Paragraph*)pList->First();
			while (pParagraph)
			{
				if( !pOutliner->HasParaFlag( pParagraph, PARAFLAG_ISPAGE ) && (pOutliner->GetDepth( pOutliner->GetAbsPos( pParagraph ) ) <= 0) )
					mnPagesToProcess++;
				pParagraph = (Paragraph*)pList->Next();
			}

			mnPagesToProcess++;	// der Absatz, der jetzt schon auf Level 0
								// steht, gehoert auch dazu
			mnPagesProcessed = 0;

			if (mnPagesToProcess > PROCESS_WITH_PROGRESS_THRESHOLD)
			{
				if( mpProgress )
					delete mpProgress;

				const String aStr(SdResId(STR_CREATE_PAGES));
				mpProgress = new SfxProgress( GetDocSh(), aStr, mnPagesToProcess );
			}
			else
			{
				mpDocSh->SetWaitCursor( sal_True );
			}
			delete pList;
		}

		ParagraphInsertedHdl(pOutliner);

		mnPagesProcessed++;

		// muss eine Fortschrittsanzeige gepflegt werden?
		if (mnPagesToProcess > PROCESS_WITH_PROGRESS_THRESHOLD)
		{
			if (mpProgress)
				mpProgress->SetState(mnPagesProcessed);
		}

		// war das die letzte Seite?
		if (mnPagesProcessed == mnPagesToProcess)
		{
			if (mnPagesToProcess > PROCESS_WITH_PROGRESS_THRESHOLD && mpProgress)
			{
				delete mpProgress;
				mpProgress = NULL;
			}
			else
				mpDocSh->SetWaitCursor( sal_False );

			mnPagesToProcess = 0;
			mnPagesProcessed = 0;
		}
		pOutliner->UpdateFields();
	}
	else if( !pOutliner->HasParaFlag( pPara, PARAFLAG_ISPAGE ) && ((pOutliner->GetPrevFlags() & PARAFLAG_ISPAGE) != 0) )
	{
		// the paragraph was a page but now becomes a normal paragraph

		// how many titles are before the title paragraph in question?
		sal_uLong nPos = 0L;
		Paragraph* pParagraph = pPara;
		while(pParagraph)
		{
			pParagraph = GetPrevTitle(pParagraph);
			if (pParagraph)
				nPos++;
		}
		// Seite und Notizseite loeschen

		sal_uInt16 nAbsPos = (sal_uInt16)nPos * 2 + 1;
		SdrPage* pPage = mpDoc->GetPage(nAbsPos);
		if( isRecordingUndo() )
			AddUndo(mpDoc->GetSdrUndoFactory().CreateUndoDeletePage(*pPage));
		mpDoc->RemovePage(nAbsPos);

		nAbsPos = (sal_uInt16)nPos * 2 + 1;
		pPage = mpDoc->GetPage(nAbsPos);
		if( isRecordingUndo() )
			AddUndo(mpDoc->GetSdrUndoFactory().CreateUndoDeletePage(*pPage));
		mpDoc->RemovePage(nAbsPos);

		pPage = GetPageForParagraph( pPara );

		mpOutliner->SetDepth( pPara, (pPage && (static_cast<SdPage*>(pPage)->GetAutoLayout() == AUTOLAYOUT_TITLE)) ?  -1 : 0 );

		// ggfs. Fortschrittsanzeige
		if (mnPagesToProcess)
		{
			mnPagesProcessed++;
			if (mpProgress)
				mpProgress->SetState(mnPagesProcessed);

			if (mnPagesProcessed == mnPagesToProcess)
			{
				if(mpProgress)
				{
					delete mpProgress;
					mpProgress = NULL;
				}
				mnPagesToProcess = 0;
				mnPagesProcessed = 0;
			}
		}
		pOutliner->UpdateFields();
	}
	else if ( (pOutliner->GetPrevDepth() == 1) && ( pOutliner->GetDepth( pOutliner->GetAbsPos( pPara ) ) == 2 ) )
	{
		// wieviele Titel sind vor dem fraglichen Titelabsatz?
		sal_Int32 nPos = -1L;

		Paragraph* pParagraph = pPara;
		while(pParagraph)
		{
			pParagraph = GetPrevTitle(pParagraph);
			if (pParagraph)
				nPos++;
		}

		if(nPos >= 0)
		{
			SdPage*pPage = (SdPage*)mpDoc->GetSdPage( (sal_uInt16) nPos, PK_STANDARD);

			if(pPage && pPage->GetPresObj(PRESOBJ_TEXT))
				pOutliner->SetDepth( pPara, 0 );
		}

	}
	// wieviele Titel sind vor dem fraglichen Titelabsatz?
	sal_Int32 nPos = -1L;

	Paragraph* pTempPara = pPara;
	while(pTempPara)
	{
		pTempPara = GetPrevTitle(pTempPara);
		if (pTempPara)
			nPos++;
	}

	if( nPos >= 0 )
	{
		SdPage* pPage = (SdPage*) mpDoc->GetSdPage( (sal_uInt16) nPos, PK_STANDARD );

		if( pPage )
		{
			SfxStyleSheet* pStyleSheet = NULL;
			sal_uLong nPara = pOutliner->GetAbsPos( pPara );
			sal_Int16 nDepth = pOutliner->GetDepth( nPara );
			bool bSubTitle = pPage->GetPresObj(PRESOBJ_TEXT) != NULL;

			if( pOutliner->HasParaFlag(pPara, PARAFLAG_ISPAGE) )
			{
				pStyleSheet = pPage->GetStyleSheetForPresObj( PRESOBJ_TITLE );
			}
			else if( bSubTitle )
			{
				pStyleSheet = pPage->GetStyleSheetForPresObj( PRESOBJ_TEXT );
			}
			else
			{
				pStyleSheet = pPage->GetStyleSheetForPresObj( PRESOBJ_OUTLINE );

				if( nDepth > 0 )
				{
					String aNewStyleSheetName( pStyleSheet->GetName() );
					aNewStyleSheetName.Erase( aNewStyleSheetName.Len()-1, 1 );
					aNewStyleSheetName += String::CreateFromInt32( nDepth+1 );
					SfxStyleSheetBasePool* pStylePool = mpDoc->GetStyleSheetPool();
					pStyleSheet = (SfxStyleSheet*) pStylePool->Find( aNewStyleSheetName, pStyleSheet->GetFamily() );
				}
			}

			// before we set the style sheet we need to preserve the bullet item
			// since all items will be deleted while setting a new style sheet
 			SfxItemSet aOldAttrs( pOutliner->GetParaAttribs( nPara ) );

			pOutliner->SetStyleSheet( nPara, pStyleSheet );

			// restore the old bullet item but not if the style changed
			if ( pOutliner->GetPrevDepth() != -1 && nDepth != -1 &&
				 aOldAttrs.GetItemState( EE_PARA_NUMBULLET ) == SFX_ITEM_ON )
			{
				SfxItemSet aAttrs( pOutliner->GetParaAttribs( nPara ) );
				aAttrs.Put( *aOldAttrs.GetItem( EE_PARA_NUMBULLET ) );
				pOutliner->SetParaAttribs( nPara, aAttrs );
			}
		}
	}

    InvalidateSlideNumberArea();

	return 0;
}

/*************************************************************************
|*
|* Handler fuer StatusEvents
|*
\************************************************************************/

IMPL_LINK( OutlineView, StatusEventHdl, EditStatus *, EMPTYARG )
{
    ::sd::Window*   pWin = mpOutlineViewShell->GetActiveWindow();
	OutlinerView*	pOutlinerView = GetViewByWindow(pWin);
	Rectangle	  aVis			= pOutlinerView->GetVisArea();

//	  sal_uLong nWidth = ((SdPage*)mpDoc->GetSdPage(0, PK_STANDARD))->GetSize().Width();
	sal_uLong nWidth = OUTLINE_PAPERWIDTH;
	Rectangle aText = Rectangle(Point(0,0),
								   Size(nWidth,
										mpOutliner->GetTextHeight()));
	Rectangle aWin(Point(0,0), pWin->GetOutputSizePixel());
	aWin = pWin->PixelToLogic(aWin);

	if (!aVis.IsEmpty())		// nicht beim Oeffnen
	{
		aText.Bottom() += aWin.GetHeight();

		mpOutlineViewShell->InitWindows(Point(0,0), aText.GetSize(),
									   Point(aVis.TopLeft()));
		mpOutlineViewShell->UpdateScrollBars();
	}

    InvalidateSlideNumberArea();
	return 0;
}

IMPL_LINK( OutlineView, BeginDropHdl, void *, EMPTYARG )
{
	DBG_ASSERT(maDragAndDropModelGuard.get() == 0, "sd::OutlineView::BeginDropHdl(), prior drag operation not finished correctly!" );

	maDragAndDropModelGuard.reset( new OutlineViewModelChangeGuard( *this ) );
	return 0;
}

IMPL_LINK( OutlineView, EndDropHdl, void *, EMPTYARG )
{
	maDragAndDropModelGuard.reset(0);
    InvalidateSlideNumberArea();
	return 0;
}

/*************************************************************************
|*
|* Handler fuer den Beginn einer Absatzverschiebung
|*
\************************************************************************/

IMPL_LINK( OutlineView, BeginMovingHdl, ::Outliner *, pOutliner )
{
	DBG_ASSERT(!mpSelectedParas, "Absatzliste nicht geloescht");
	DBG_ASSERT(!mpOldParaOrder, "Absatzliste nicht geloescht");

	OutlineViewPageChangesGuard aGuard(this);

	mpOldParaOrder = new List;

	// Liste der selektierten Titelabsaetze
	mpSelectedParas = mpOutlinerView[0]->CreateSelectionList();
	Paragraph* pPara = static_cast<Paragraph*>(mpSelectedParas->First());
	while (pPara)
	{
		if( !pOutliner->HasParaFlag(pPara, PARAFLAG_ISPAGE) )
		{
			mpSelectedParas->Remove();
			pPara = static_cast<Paragraph*>(mpSelectedParas->GetCurObject());
		}
		else
		{
			pPara = static_cast<Paragraph*>(mpSelectedParas->Next());
		}
	}

	// Die zu den selektierten Absaetzen auf Ebene 0 gehoerenden Seiten
	// selektieren
	sal_uInt16 nPos = 0;
	sal_uLong nParaPos = 0;
	pPara = pOutliner->GetParagraph( 0 );

	while(pPara)
	{
		if( pOutliner->HasParaFlag(pPara, PARAFLAG_ISPAGE) )                     // eine Seite?
		{
			mpOldParaOrder->Insert(pPara, LIST_APPEND);
			SdPage* pPage = mpDoc->GetSdPage(nPos, PK_STANDARD);
			pPage->SetSelected(sal_False);
			if (mpSelectedParas->Seek(pPara))            // selektiert?
			{
				pPage->SetSelected(sal_True);
			}
			nPos++;
		}
		pPara = pOutliner->GetParagraph( ++nParaPos );
	}

	return 0;
}

/*************************************************************************
|*
|* Handler fuer das Ende einer Absatzverschiebung
|*
\************************************************************************/

IMPL_LINK( OutlineView, EndMovingHdl, ::Outliner *, pOutliner )
{
	OutlineViewPageChangesGuard aGuard(this);

	DBG_ASSERT(mpSelectedParas, "keine Absatzliste");
	DBG_ASSERT(mpOldParaOrder, "keine Absatzliste");
	DBG_ASSERT( isRecordingUndo(), "sd::OutlineView::EndMovingHdl(), model change without undo?!" );

	// Einfuegeposition anhand des ersten Absatzes suchen
	Paragraph* pSearchIt = (Paragraph*)mpSelectedParas->First();

	// den ersten der selektierten Paragraphen in der neuen Ordnung suchen
	sal_uInt16 nPosNewOrder = 0;
	sal_uLong nParaPos = 0;
	Paragraph*	pPara = pOutliner->GetParagraph( 0 );
	Paragraph*	pPrev = NULL;
	while (pPara && pPara != pSearchIt)
	{
		if( pOutliner->HasParaFlag(pPara, PARAFLAG_ISPAGE) )
		{
			nPosNewOrder++;
			pPrev = pPara;
		}
		pPara = pOutliner->GetParagraph( ++nParaPos );
	}

	sal_uInt16 nPos = nPosNewOrder; 	// nPosNewOrder nicht veraendern
	if (nPos == 0)
	{
		nPos = (sal_uInt16)-1;			// vor der ersten Seite einfuegen
	}
	else
	{
		// den Vorgaenger in der alten Ordnung suchen
		nPos = (sal_uInt16)mpOldParaOrder->GetPos(pPrev);
		DBG_ASSERT(nPos != 0xffff, "Absatz nicht gefunden");
	}

	mpDoc->MovePages(nPos);

	// die Seiten wieder deselektieren
	sal_uInt16 nPageCount = (sal_uInt16)mpSelectedParas->Count();
	while (nPageCount)
	{
		SdPage* pPage = mpDoc->GetSdPage(nPosNewOrder, PK_STANDARD);
		pPage->SetSelected(sal_False);
		nPosNewOrder++;
		nPageCount--;
	}

	pOutliner->UpdateFields();

	delete mpSelectedParas;
	mpSelectedParas = NULL;
	delete mpOldParaOrder;
	mpOldParaOrder = NULL;

    InvalidateSlideNumberArea();

	return 0;
}

/*************************************************************************
|*
|* Eine Seite des Models nach dem Titeltextobjekt durchsuchen
|*
\************************************************************************/

SdrTextObj* OutlineView::GetTitleTextObject(SdrPage* pPage)
{
	sal_uLong			nObjectCount = pPage->GetObjCount();
	SdrObject*		pObject 	 = NULL;
	SdrTextObj* 	pResult 	 = NULL;

	for (sal_uLong nObject = 0; nObject < nObjectCount; nObject++)
	{
		pObject = pPage->GetObj(nObject);
		if (pObject->GetObjInventor() == SdrInventor &&
			pObject->GetObjIdentifier() == OBJ_TITLETEXT)
		{
			pResult = (SdrTextObj*)pObject;
			break;
		}
	}
	return pResult;
}


/*************************************************************************
|*
|* Eine Seite des Models nach dem Gliederungstextobjekt durchsuchen
|*
\************************************************************************/

SdrTextObj* OutlineView::GetOutlineTextObject(SdrPage* pPage)
{
	sal_uLong			nObjectCount = pPage->GetObjCount();
	SdrObject*		pObject 	 = NULL;
	SdrTextObj* 	pResult 	 = NULL;

	for (sal_uLong nObject = 0; nObject < nObjectCount; nObject++)
	{
		pObject = pPage->GetObj(nObject);
		if (pObject->GetObjInventor() == SdrInventor &&
			pObject->GetObjIdentifier() == OBJ_OUTLINETEXT)
		{
			pResult = (SdrTextObj*)pObject;
			break;
		}
	}
	return pResult;
}

SdrTextObj* OutlineView::CreateTitleTextObject(SdPage* pPage)
{
	DBG_ASSERT( GetTitleTextObject(pPage) == 0, "sd::OutlineView::CreateTitleTextObject(), there is already a title text object!" );

	if( pPage->GetAutoLayout() == AUTOLAYOUT_NONE )
	{
		// simple case
		pPage->SetAutoLayout( AUTOLAYOUT_ONLY_TITLE, true );
	}
	else
	{
		// we already have a layout with a title but the title
		// object was deleted, create a new one
		pPage->InsertAutoLayoutShape( 0, PRESOBJ_TITLE, false, pPage->GetTitleRect(), true );
	}

	return GetTitleTextObject(pPage);
}

SdrTextObj* OutlineView::CreateOutlineTextObject(SdPage* pPage)
{
	DBG_ASSERT( GetOutlineTextObject(pPage) == 0, "sd::OutlineView::CreateOutlineTextObject(), there is already a layout text object!" );

	AutoLayout eNewLayout = pPage->GetAutoLayout();
	switch( eNewLayout )
	{
	case AUTOLAYOUT_NONE:
	case AUTOLAYOUT_ONLY_TITLE:
	case AUTOLAYOUT_TITLE:	eNewLayout = AUTOLAYOUT_ENUM; break;

	case AUTOLAYOUT_CHART:	eNewLayout = AUTOLAYOUT_CHARTTEXT; break;

	case AUTOLAYOUT_ORG:
	case AUTOLAYOUT_TAB:
	case AUTOLAYOUT_OBJ:	eNewLayout = AUTOLAYOUT_OBJTEXT; break;
	default:
		break;
	}

	if( eNewLayout != pPage->GetAutoLayout() )
	{
		pPage->SetAutoLayout( eNewLayout, true );
	}
	else
	{
		// we already have a layout with a text but the text
		// object was deleted, create a new one
		pPage->InsertAutoLayoutShape( 0,
									  (eNewLayout == AUTOLAYOUT_TITLE) ? PRESOBJ_TEXT : PRESOBJ_OUTLINE,
									  false, pPage->GetLayoutRect(), true );
	}

	return GetOutlineTextObject(pPage);
}

/** updates draw model with all changes from outliner model */
sal_Bool OutlineView::PrepareClose(sal_Bool)
{
    ::sd::UndoManager* pDocUndoMgr = dynamic_cast<sd::UndoManager*>(mpDocSh->GetUndoManager());
    if (pDocUndoMgr != NULL)
        pDocUndoMgr->SetLinkedUndoManager(NULL);

	mpOutliner->GetUndoManager().Clear();

	const String aUndoStr(SdResId(STR_UNDO_CHANGE_TITLE_AND_LAYOUT));
	BegUndo(aUndoStr);
	UpdateDocument();
	EndUndo();
	mpDoc->SetSelected(GetActualPage(), sal_True);
	return sal_True;
}


/*************************************************************************
|*
|* Attribute des selektierten Textes setzen
|*
\************************************************************************/

sal_Bool OutlineView::SetAttributes(const SfxItemSet& rSet, sal_Bool )
{
	sal_Bool bOk = sal_False;

	OutlinerView* pOlView = GetViewByWindow(mpOutlineViewShell->GetActiveWindow());

	if (pOlView)
	{
		pOlView->SetAttribs(rSet);
		bOk = sal_True;
	}

    mpOutlineViewShell->Invalidate (SID_PREVIEW_STATE);

	return (bOk);
}

/*************************************************************************
|*
|* Attribute des selektierten Textes erfragen
|*
\************************************************************************/

sal_Bool OutlineView::GetAttributes( SfxItemSet& rTargetSet, sal_Bool ) const
{
	OutlinerView* pOlView = GetViewByWindow(
								mpOutlineViewShell->GetActiveWindow());
	DBG_ASSERT(pOlView, "keine OutlinerView gefunden");

	rTargetSet.Put( pOlView->GetAttribs(), sal_False );
	return sal_True;
}

/** creates outliner model from draw model */
void OutlineView::FillOutliner()
{
	mpOutliner->GetUndoManager().Clear();
	mpOutliner->EnableUndo(sal_False);
	ResetLinks();
    mpOutliner->SetUpdateMode(false);

	Paragraph* pTitleToSelect = NULL;
	sal_uLong nPageCount = mpDoc->GetSdPageCount(PK_STANDARD);

	// fill outliner with paragraphs from slides title & (outlines|subtitles)
	for (sal_uInt16 nPage = 0; nPage < nPageCount; nPage++)
	{
		SdPage* 	pPage = (SdPage*)mpDoc->GetSdPage(nPage, PK_STANDARD);
		Paragraph * pPara = NULL;

        // take text from title shape
		SdrTextObj* pTO = GetTitleTextObject(pPage);
		if(pTO && !(pTO->IsEmptyPresObj()))
		{
			OutlinerParaObject* pOPO = pTO->GetOutlinerParaObject();
			if (pOPO)
			{
				sal_Bool bVertical = pOPO->IsVertical();
				pOPO->SetVertical( sal_False );
				mpOutliner->AddText(*pOPO);
				pOPO->SetVertical( bVertical );
				pPara = mpOutliner->GetParagraph( mpOutliner->GetParagraphCount()-1 );
			}
		}

		if( pPara == 0 ) // no title, insert an empty paragraph
		{
			pPara = mpOutliner->Insert(String());
			mpOutliner->SetDepth(pPara, -1);

			// Keine harten Attribute vom vorherigen Absatz uebernehmen
			mpOutliner->SetParaAttribs( mpOutliner->GetAbsPos(pPara),
									   mpOutliner->GetEmptyItemSet() );

			mpOutliner->SetStyleSheet( mpOutliner->GetAbsPos( pPara ), pPage->GetStyleSheetForPresObj( PRESOBJ_TITLE ) );
		}

		mpOutliner->SetParaFlag( pPara, PARAFLAG_ISPAGE );

		sal_uLong nPara = mpOutliner->GetAbsPos( pPara );

		UpdateParagraph( (sal_uInt16)nPara );

		// remember paragraph of currently selected page
		if (pPage->IsSelected())
			pTitleToSelect = pPara;

		// take text from subtitle or outline
		pTO = static_cast<SdrTextObj*>(pPage->GetPresObj(PRESOBJ_TEXT));
		const bool bSubTitle = pTO != 0;

		if (!pTO) // if no subtile found, try outline
			pTO = GetOutlineTextObject(pPage);
		
		if(pTO && !(pTO->IsEmptyPresObj())) // found some text
		{
			OutlinerParaObject* pOPO = pTO->GetOutlinerParaObject();
			if (pOPO)
			{
				sal_uInt32 nParaCount1 = mpOutliner->GetParagraphCount();
				sal_Bool bVertical = pOPO->IsVertical();
				pOPO->SetVertical( sal_False );
				mpOutliner->AddText(*pOPO);
				pOPO->SetVertical( bVertical );

                sal_uInt32 nParaCount2 = mpOutliner->GetParagraphCount();
				for (sal_uInt32 n = nParaCount1; n < nParaCount2; n++)
				{
                    if( bSubTitle )
                    {
					    Paragraph* p = mpOutliner->GetParagraph(n);
					    if(p && mpOutliner->GetDepth( n ) > 0 )
						    mpOutliner->SetDepth(p, 0);
                    }

					UpdateParagraph( n );
				}
			}
		}
	}

	// place cursor at the start
	Paragraph* pFirstPara = mpOutliner->GetParagraph( 0 );
	mpOutlinerView[0]->Select( pFirstPara, sal_True, sal_False );
	mpOutlinerView[0]->Select( pFirstPara, sal_False, sal_False );

	// select title of slide that was selected
	if (pTitleToSelect)
		mpOutlinerView[0]->Select(pTitleToSelect, sal_True, sal_False);

	SetLinks();

	mpOutliner->EnableUndo(sal_True);

    mpOutliner->SetUpdateMode(true);
}

/*************************************************************************
|*
|* Handler fuer das Loeschen von Level-0-Absaetzen (Seiten): Warnung
|*
\************************************************************************/

IMPL_LINK( OutlineView, RemovingPagesHdl, OutlinerView *, EMPTYARG )
{
	sal_uInt16 nNumOfPages = mpOutliner->GetSelPageCount();

	if (nNumOfPages > PROCESS_WITH_PROGRESS_THRESHOLD)
	{
		mnPagesToProcess = nNumOfPages;
		mnPagesProcessed  = 0;
	}

	if (mnPagesToProcess)
	{
		if( mpProgress )
			delete mpProgress;

		String aStr(SdResId(STR_DELETE_PAGES));
		mpProgress = new SfxProgress( GetDocSh(), aStr, mnPagesToProcess );
	}
	mpOutliner->UpdateFields();

    InvalidateSlideNumberArea();

	return 1;
}

/*************************************************************************
|*
|* Handler fuer das Einruecken von Level-0-Absaetzen (Seiten): Warnung
|*
\************************************************************************/

IMPL_LINK_INLINE_START( OutlineView, IndentingPagesHdl, OutlinerView *, pOutlinerView )
{
	return RemovingPagesHdl(pOutlinerView);
}
IMPL_LINK_INLINE_END( OutlineView, IndentingPagesHdl, OutlinerView *, pOutlinerView )


/** returns the first slide that is selected in the outliner or where
	the cursor is located */
SdPage* OutlineView::GetActualPage()
{
    ::sd::Window* pWin = mpOutlineViewShell->GetActiveWindow();
	OutlinerView* pActiveView = GetViewByWindow(pWin);
	std::auto_ptr<List> pSelList( static_cast< List* >(pActiveView->CreateSelectionList()) );

	SdPage* pCurrent = GetPageForParagraph(static_cast<Paragraph*>(pSelList->First()) );
	DBG_ASSERT( pCurrent ||
				(mpDocSh->GetUndoManager() && static_cast< sd::UndoManager *>(mpDocSh->GetUndoManager())->IsDoing()) ||
				maDragAndDropModelGuard.get(),
				"sd::OutlineView::GetActualPage(), no current page?" );
	if( pCurrent )
		return pCurrent;
	else
		return mpDoc->GetSdPage( 0, PK_STANDARD );
}

SdPage* OutlineView::GetPageForParagraph( Paragraph* pPara )
{
	if( !mpOutliner->HasParaFlag(pPara,PARAFLAG_ISPAGE) )
		pPara = GetPrevTitle(pPara);

	sal_uInt32 nPageToSelect = 0;
	while(pPara)
	{
		pPara = GetPrevTitle(pPara);
		if(pPara)
			nPageToSelect++;
	}

	if( nPageToSelect < (sal_uInt32)mpDoc->GetSdPageCount( PK_STANDARD ) )
		return static_cast< SdPage* >( mpDoc->GetSdPage( (sal_uInt16)nPageToSelect, PK_STANDARD) );
	else
		return 0;
}

Paragraph* OutlineView::GetParagraphForPage( ::Outliner* pOutl, SdPage* pPage )
{
	// get the number of paragraphs with ident 0 we need to skip before
	// we finde the actual page
	sal_uInt32 nPagesToSkip = (pPage->GetPageNum() - 1) >> 1;

	sal_uInt32 nParaPos = 0;
	Paragraph* pPara = pOutl->GetParagraph( 0 );
	while( pPara )
	{
		// if this paragraph is a page ...
		if( mpOutliner->HasParaFlag(pPara,PARAFLAG_ISPAGE) )
		{
			// see if we already skiped enough pages
			if( 0 == nPagesToSkip )
				break;	// and if so, end the loop

			// we skiped another page
			nPagesToSkip--;
		}

		// get next paragraph
		pPara = mpOutliner->GetParagraph( ++nParaPos );
	}

	return pPara;
}

/** selects the paragraph for the given page at the outliner view*/
void OutlineView::SetActualPage( SdPage* pActual )
{
	if( pActual && mpOutliner && dynamic_cast<Outliner*> ( mpOutliner )->GetIgnoreCurrentPageChangesLevel()==0 && !mbFirstPaint)
	{
		// if we found a paragraph, select its text at the outliner view
		Paragraph* pPara = GetParagraphForPage( mpOutliner, pActual );
		if( pPara )
			mpOutlinerView[0]->Select( pPara, sal_True, sal_False );
	}
}

/*************************************************************************
|*
|* StyleSheet aus der Selektion besorgen
|*
\************************************************************************/

SfxStyleSheet* OutlineView::GetStyleSheet() const
{
 	::sd::Window* pActWin = mpOutlineViewShell->GetActiveWindow();
	OutlinerView* pOlView = GetViewByWindow(pActWin);
	SfxStyleSheet* pResult = pOlView->GetStyleSheet();
	return pResult;
}



/*************************************************************************
|*
|* Seiten als selektiert / nicht selektiert setzen
|*
\************************************************************************/

void OutlineView::SetSelectedPages()
{
	// Liste der selektierten Titelabsaetze
	List* pSelParas = mpOutlinerView[0]->CreateSelectionList();
	Paragraph* pPara = (Paragraph*) pSelParas->First();

	while(pPara)
	{
		if( !mpOutliner->HasParaFlag(pPara, PARAFLAG_ISPAGE) )
		{
			pSelParas->Remove();
			pPara = (Paragraph*) pSelParas->GetCurObject();
		}
		else
		{
			pPara = (Paragraph*) pSelParas->Next();
		}
	}

	// Die zu den selektierten Absaetzen auf Ebene 0 gehoerenden Seiten
	// selektieren
	sal_uInt16 nPos = 0;
	sal_uLong nParaPos = 0;
	pPara = mpOutliner->GetParagraph( 0 );

	while(pPara)
	{
		if( mpOutliner->HasParaFlag(pPara, PARAFLAG_ISPAGE) )                     // eine Seite?
		{
			SdPage* pPage = mpDoc->GetSdPage(nPos, PK_STANDARD);
            DBG_ASSERT(pPage!=NULL,
                "Trying to select non-existing page OutlineView::SetSelectedPages()");
            if (pPage != NULL)
            {
                pPage->SetSelected(sal_False);

                if (pSelParas->Seek(pPara))            // selektiert?
                    pPage->SetSelected(sal_True);
			}

			nPos++;
		}

		pPara = mpOutliner->GetParagraph( ++nParaPos );
	}
}


/*************************************************************************
|*
|* Neue Links setzen
|*
\************************************************************************/

void OutlineView::SetLinks()
{
	// Benachrichtigungs-Links setzen
	mpOutliner->SetParaInsertedHdl(LINK(this, OutlineView, ParagraphInsertedHdl));
	mpOutliner->SetParaRemovingHdl(LINK(this, OutlineView, ParagraphRemovingHdl));
	mpOutliner->SetDepthChangedHdl(LINK(this, OutlineView, DepthChangedHdl));
	mpOutliner->SetBeginMovingHdl(LINK(this, OutlineView, BeginMovingHdl));
	mpOutliner->SetEndMovingHdl(LINK(this, OutlineView, EndMovingHdl));
	mpOutliner->SetRemovingPagesHdl(LINK(this, OutlineView, RemovingPagesHdl));
	mpOutliner->SetIndentingPagesHdl(LINK(this, OutlineView, IndentingPagesHdl));
	mpOutliner->SetStatusEventHdl(LINK(this, OutlineView, StatusEventHdl));
	mpOutliner->SetBeginDropHdl(LINK(this,OutlineView, BeginDropHdl));
	mpOutliner->SetEndDropHdl(LINK(this,OutlineView, EndDropHdl));
    mpOutliner->SetPaintFirstLineHdl(LINK(this,OutlineView,PaintingFirstLineHdl));
    mpOutliner->SetBeginPasteOrDropHdl(LINK(this,OutlineView, BeginPasteOrDropHdl));
    mpOutliner->SetEndPasteOrDropHdl(LINK(this,OutlineView, EndPasteOrDropHdl));
}



/*************************************************************************
|*
|* Alte Links restaurieren
|*
\************************************************************************/

void OutlineView::ResetLinks() const
{
	// alte Links restaurieren
	Link aEmptyLink;
	mpOutliner->SetParaInsertedHdl(aEmptyLink);
	mpOutliner->SetParaRemovingHdl(aEmptyLink);
	mpOutliner->SetDepthChangedHdl(aEmptyLink);
	mpOutliner->SetBeginMovingHdl(aEmptyLink);
	mpOutliner->SetEndMovingHdl(aEmptyLink);
	mpOutliner->SetStatusEventHdl(aEmptyLink);
	mpOutliner->SetRemovingPagesHdl(aEmptyLink);
	mpOutliner->SetIndentingPagesHdl(aEmptyLink);
    mpOutliner->SetDrawPortionHdl(aEmptyLink);
    mpOutliner->SetBeginPasteOrDropHdl(aEmptyLink);
    mpOutliner->SetEndPasteOrDropHdl(aEmptyLink);
}

/*************************************************************************
|*
|* AcceptDrop
|*
\************************************************************************/

sal_Int8 OutlineView::AcceptDrop( const AcceptDropEvent&, DropTargetHelper&, ::sd::Window*, sal_uInt16, sal_uInt16)
{
	return DND_ACTION_NONE;
}

/*************************************************************************
|*
|* ExecuteDrop
|*
\************************************************************************/

sal_Int8 OutlineView::ExecuteDrop( const ExecuteDropEvent&, DropTargetHelper&, ::sd::Window*, sal_uInt16, sal_uInt16)
{
	return DND_ACTION_NONE;
}

// #97766# Re-implement GetScriptType for this view to get correct results
sal_uInt16 OutlineView::GetScriptType() const
{
	sal_uInt16 nScriptType = ::sd::View::GetScriptType();

	if(mpOutliner)
	{
		OutlinerParaObject* pTempOPObj = mpOutliner->CreateParaObject();

		if(pTempOPObj)
		{
			nScriptType = pTempOPObj->GetTextObject().GetScriptType();
			delete pTempOPObj;
		}
	}

	return nScriptType;
}

void OutlineView::onUpdateStyleSettings( bool bForceUpdate /* = false */ )
{
	const bool bHighContrastMode = Application::GetSettings().GetStyleSettings().GetHighContrastMode() != 0;
	if( bForceUpdate || (mbHighContrastMode != bHighContrastMode) )
	{
		if( mpOutliner )
		{
			mpOutliner->ForceAutoColor( bHighContrastMode );
		}
		mbHighContrastMode = bHighContrastMode;

	}

    svtools::ColorConfig aColorConfig;
    const Color aDocColor( aColorConfig.GetColorValue( svtools::DOCCOLOR ).nColor );
	if( bForceUpdate || (maDocColor != aDocColor) )
	{
		sal_uInt16 nView;
		for( nView = 0; nView < MAX_OUTLINERVIEWS; nView++ )
		{
			if (mpOutlinerView[nView] != NULL)
			{
				mpOutlinerView[nView]->SetBackgroundColor( aDocColor );

				::Window* pWindow = mpOutlinerView[nView]->GetWindow();

				if( pWindow )
					pWindow->SetBackground( Wallpaper( aDocColor ) );

			}
		}

		if( mpOutliner )
			mpOutliner->SetBackgroundColor( aDocColor );

		maDocColor = aDocColor;
	}
}

IMPL_LINK( OutlineView, AppEventListenerHdl, void *, EMPTYARG )
{
	onUpdateStyleSettings();
	return 0;
}




IMPL_LINK(OutlineView, EventMultiplexerListener, ::sd::tools::EventMultiplexerEvent*, pEvent)
{
    if (pEvent != NULL)
    {
        switch (pEvent->meEventId)
        {
            case tools::EventMultiplexerEvent::EID_CURRENT_PAGE:
                SetActualPage(mpOutlineViewShell->GetActualPage());
                InvalidateSlideNumberArea();
                break;

            case tools::EventMultiplexerEvent::EID_PAGE_ORDER:
                if (mpOutliner != NULL && mpDoc!=NULL && mpOutliner != NULL && dynamic_cast<Outliner*> ( mpOutliner )->GetIgnoreCurrentPageChangesLevel()==0)
                {
                    if (((mpDoc->GetPageCount()-1)%2) == 0)
                    {
                        mpOutliner->Clear();
                        FillOutliner();
                        ::sd::Window* pWindow = mpOutlineViewShell->GetActiveWindow();
                        if (pWindow != NULL)
                            pWindow->Invalidate();
                    }
                }
                break;
        }
    }
    return 0;
}

void OutlineView::IgnoreCurrentPageChanges (bool bIgnoreChanges)
{
	if ( mpOutliner )
	{
		if (bIgnoreChanges)
			dynamic_cast<Outliner*> ( mpOutliner )->IncreIgnoreCurrentPageChangesLevel();
		else
			dynamic_cast<Outliner*> ( mpOutliner )->DecreIgnoreCurrentPageChangesLevel();
	}
}

/** call this method before you do anything that can modify the outliner
	and or the drawing document model. It will create needed undo actions */
void OutlineView::BeginModelChange()
{
	const String aEmpty;
	mpOutliner->GetUndoManager().EnterListAction(aEmpty,aEmpty);
	const String aUndoStr(SdResId(STR_UNDO_CHANGE_TITLE_AND_LAYOUT));
	BegUndo(aUndoStr);
}

/** call this method after BeginModelChange(), when all possible model
	changes are done. */
void OutlineView::EndModelChange()
{
	UpdateDocument();

	::svl::IUndoManager* pDocUndoMgr = mpDocSh->GetUndoManager();

	bool bHasUndoActions = pDocUndoMgr->GetUndoActionCount() != 0;

	EndUndo();

	DBG_ASSERT( bHasUndoActions == (mpOutliner->GetUndoManager().GetUndoActionCount() != 0), "sd::OutlineView::EndModelChange(), undo actions not in sync!" );

	if( bHasUndoActions )
	{
		SfxLinkUndoAction* pLink = new SfxLinkUndoAction(pDocUndoMgr);
		mpOutliner->GetUndoManager().AddUndoAction(pLink);
	}

	mpOutliner->GetUndoManager().LeaveListAction();

	if( bHasUndoActions && mpOutliner->GetEditEngine().HasTriedMergeOnLastAddUndo() )
		TryToMergeUndoActions();

    mpOutlineViewShell->Invalidate( SID_UNDO );
    mpOutlineViewShell->Invalidate( SID_REDO );
}

/** updates all changes in the outliner model to the draw model */
void OutlineView::UpdateDocument()
{
	const sal_uInt32 nPageCount = mpDoc->GetSdPageCount(PK_STANDARD);
	Paragraph* pPara = mpOutliner->GetParagraph( 0 );
	sal_uInt32 nPage;
	for (nPage = 0; nPage < nPageCount; nPage++)
	{
		SdPage* pPage = mpDoc->GetSdPage( (sal_uInt16)nPage, PK_STANDARD);
		mpDoc->SetSelected(pPage, sal_False);

		mpOutlineViewShell->UpdateTitleObject( pPage, pPara );
		mpOutlineViewShell->UpdateOutlineObject( pPage, pPara );

		if( pPara )
			pPara = GetNextTitle(pPara);
	}

	DBG_ASSERT( pPara == 0, "sd::OutlineView::UpdateDocument(), slides are out of sync, creating missing ones" );
	while( pPara )
	{
		SdPage* pPage = InsertSlideForParagraph( pPara );
		mpDoc->SetSelected(pPage, sal_False);

		mpOutlineViewShell->UpdateTitleObject( pPage, pPara );
		mpOutlineViewShell->UpdateOutlineObject( pPage, pPara );

		if( pPara )
			pPara = GetNextTitle(pPara);
	}
}

/** merge edit engine undo actions if possible */
void OutlineView::TryToMergeUndoActions()
{
	::svl::IUndoManager& rOutlineUndo = mpOutliner->GetUndoManager();
	if( rOutlineUndo.GetUndoActionCount() > 1 )
	{
		SfxListUndoAction* pListAction = dynamic_cast< SfxListUndoAction* >( rOutlineUndo.GetUndoAction(0) );
		SfxListUndoAction* pPrevListAction = dynamic_cast< SfxListUndoAction* >( rOutlineUndo.GetUndoAction(1) );
		if( pListAction && pPrevListAction )
		{
			// find the top EditUndo action in the top undo action list
			size_t nAction = pListAction->aUndoActions.size();
			EditUndo* pEditUndo = 0;
			while( !pEditUndo && nAction )
			{
				pEditUndo = dynamic_cast< EditUndo* >(pListAction->aUndoActions[--nAction].pAction);
			}

			sal_uInt16 nEditPos = nAction; // we need this later to remove the merged undo actions

			// make sure it is the only EditUndo action in the top undo list
			while( pEditUndo && nAction )
			{
				if( dynamic_cast< EditUndo* >(pListAction->aUndoActions[--nAction].pAction) )
					pEditUndo = 0;
			}

			// do we have one and only one EditUndo action in the top undo list?
			if( pEditUndo )
			{
				// yes, see if we can merge it with the prev undo list

				nAction = pPrevListAction->aUndoActions.size();
				EditUndo* pPrevEditUndo = 0;
				while( !pPrevEditUndo && nAction )
					pPrevEditUndo = dynamic_cast< EditUndo* >(pPrevListAction->aUndoActions[--nAction].pAction);

				if( pPrevEditUndo && pPrevEditUndo->Merge( pEditUndo ) )
				{
					// ok we merged the only EditUndo of the top undo list with
					// the top EditUndo of the previous undo list

					// first remove the merged undo action
					DBG_ASSERT( pListAction->aUndoActions[nEditPos].pAction == pEditUndo,
                        "sd::OutlineView::TryToMergeUndoActions(), wrong edit pos!" );
					pListAction->aUndoActions.Remove(nEditPos);
					delete pEditUndo;

					// now check if we also can merge the draw undo actions
					::svl::IUndoManager* pDocUndoManager = mpDocSh->GetUndoManager();
					if( pDocUndoManager && ( pListAction->aUndoActions.size() == 1 ))
					{
						SfxLinkUndoAction* pLinkAction = dynamic_cast< SfxLinkUndoAction* >( pListAction->aUndoActions[0].pAction );
						SfxLinkUndoAction* pPrevLinkAction = 0;

						if( pLinkAction )
						{
							nAction = pPrevListAction->aUndoActions.size();
							while( !pPrevLinkAction && nAction )
								pPrevLinkAction = dynamic_cast< SfxLinkUndoAction* >(pPrevListAction->aUndoActions[--nAction].pAction);
						}

						if( pLinkAction && pPrevLinkAction &&
							( pLinkAction->GetAction() == pDocUndoManager->GetUndoAction(0) ) &&
							( pPrevLinkAction->GetAction() == pDocUndoManager->GetUndoAction(1) ) )
						{
							SfxListUndoAction* pSourceList = dynamic_cast< SfxListUndoAction* >(pLinkAction->GetAction());
							SfxListUndoAction* pDestinationList = dynamic_cast< SfxListUndoAction* >(pPrevLinkAction->GetAction());

							if( pSourceList && pDestinationList )
							{
								sal_uInt16 nCount = pSourceList->aUndoActions.size();
								sal_uInt16 nDestAction = pDestinationList->aUndoActions.size();
								while( nCount-- )
								{
									SfxUndoAction* pTemp = pSourceList->aUndoActions[0].pAction;
									pSourceList->aUndoActions.Remove(0);
									pDestinationList->aUndoActions.Insert( pTemp, nDestAction++ );
								}
								pDestinationList->nCurUndoAction = pDestinationList->aUndoActions.size();

								pListAction->aUndoActions.Remove(0);
								delete pLinkAction;

								pDocUndoManager->RemoveLastUndoAction();
							}
						}
					}

					if ( !pListAction->aUndoActions.empty() )
					{
						// now we have to move all remaining doc undo actions from the top undo
						// list to the previous undo list and remove the top undo list

						size_t nCount = pListAction->aUndoActions.size();
						size_t nDestAction = pPrevListAction->aUndoActions.size();
						while( nCount-- )
						{
							SfxUndoAction* pTemp = pListAction->aUndoActions[0].pAction;
							pListAction->aUndoActions.Remove(0);
							if( pTemp )
								pPrevListAction->aUndoActions.Insert( pTemp, nDestAction++ );
						}
						pPrevListAction->nCurUndoAction = pPrevListAction->aUndoActions.size();
					}

					rOutlineUndo.RemoveLastUndoAction();
				}
			}
		}
	}
}

IMPL_LINK(OutlineView, PaintingFirstLineHdl, PaintFirstLineInfo*, pInfo)
{
    if( pInfo && mpOutliner )
    {
        Paragraph* pPara = mpOutliner->GetParagraph( pInfo->mnPara );
        EditEngine& rEditEngine = const_cast< EditEngine& >( mpOutliner->GetEditEngine() );

		Size aImageSize( pInfo->mpOutDev->PixelToLogic( maSlideImage.GetSizePixel()  ) );
		Size aOffset( 100, 100 );

	    // paint slide number
	    if( pPara && mpOutliner->HasParaFlag(pPara,PARAFLAG_ISPAGE) )
	    {
		    long nPage = 0; // todo, printing??
		    for ( sal_uInt16 n = 0; n <= pInfo->mnPara; n++ )
		    {
			    Paragraph* p = mpOutliner->GetParagraph( n );
			    if ( mpOutliner->HasParaFlag(p,PARAFLAG_ISPAGE) )
				    nPage++;
		    }

            long nBulletHeight = (long)mpOutliner->GetLineHeight( pInfo->mnPara );
            long nFontHeight = 0;
            if ( !rEditEngine.IsFlatMode() )
            {
//		        const SvxFontHeightItem& rFH = (const SvxFontHeightItem&)rEditEngine.GetParaAttrib( pInfo->mnPara, EE_CHAR_FONTHEIGHT );
//              nBulletHeight = rFH.GetHeight();
                nFontHeight = nBulletHeight / 5;
            }
            else
            {
//		        const SvxFontHeightItem& rFH = (const SvxFontHeightItem&)rEditEngine.GetEmptyItemSet().Get( EE_CHAR_FONTHEIGHT );
 //               nBulletHeight = rFH.GetHeight();
                nFontHeight = (nBulletHeight * 10) / 25;
            }

			Size aFontSz( 0, nFontHeight );

			Size aOutSize( 2000, nBulletHeight );

			const float fImageHeight = ((float)aOutSize.Height() * (float)4) / (float)7;
			const float fImageRatio  = (float)aImageSize.Height() / (float)aImageSize.Width();
			aImageSize.Width() = (long)( fImageRatio * fImageHeight );
			aImageSize.Height() = (long)( fImageHeight );

			Point aImagePos( pInfo->mrStartPos );
			aImagePos.X() += aOutSize.Width() - aImageSize.Width() - aOffset.Width() ;
			aImagePos.Y() += (aOutSize.Height() - aImageSize.Height()) / 2;

			pInfo->mpOutDev->DrawImage( aImagePos, aImageSize, maSlideImage );

            const bool bVertical = mpOutliner->IsVertical();
            const bool bRightToLeftPara = rEditEngine.IsRightToLeft( pInfo->mnPara );

            LanguageType eLang = rEditEngine.GetDefaultLanguage();

            Point aTextPos( aImagePos.X() - aOffset.Width(), pInfo->mrStartPos.Y() );
            Font aNewFont( OutputDevice::GetDefaultFont( DEFAULTFONT_SANS_UNICODE, eLang, 0 ) );
		    aNewFont.SetSize( aFontSz );
//		    aNewFont.SetAlign( aBulletFont.GetAlign() );
		    aNewFont.SetVertical( bVertical );
		    aNewFont.SetOrientation( bVertical ? 2700 : 0 );
            aNewFont.SetColor( COL_AUTO );
		    pInfo->mpOutDev->SetFont( aNewFont );
		    String aPageText = String::CreateFromInt32( nPage );
		    Size aTextSz;
		    aTextSz.Width() = pInfo->mpOutDev->GetTextWidth( aPageText );
		    aTextSz.Height() = pInfo->mpOutDev->GetTextHeight();
//            long nBulletHeight = !bVertical ? aBulletArea.GetHeight() : aBulletArea.GetWidth();
		    if ( !bVertical )
		    {
			    aTextPos.Y() += (aOutSize.Height() - aTextSz.Height()) / 2;
                if ( !bRightToLeftPara )
                {
			        aTextPos.X() -= aTextSz.Width();
                }
                else
                {
			        aTextPos.X() += aTextSz.Width();
                }
		    }
		    else
		    {
			    aTextPos.Y() -= aTextSz.Width();
			    aTextPos.X() += nBulletHeight / 2;
		    }
		    pInfo->mpOutDev->DrawText( aTextPos, aPageText );
	    }
    }

    return 0;
}

#if 0
sal_Int32 OutlineView::GetPageNumberWidthPixel()
{
	Window* pActWin = mpOutlineViewShell->GetActiveWindow();
	if( pActWin )
	{
		Font aOldFont( pActWin->GetFont() );
		pActWin->SetFont( maPageNumberFont );
		Size aSize( pActWin->GetTextWidth( String( RTL_CONSTASCII_USTRINGPARAM("X" ) ) ), 0 );
		sal_Int32 nWidth = pActWin->LogicToPixel( aSize ).Width() * 5;

		const String aBulletStr( sal_Unicode( 0xE011 ) );
		pActWin->SetFont( maBulletFont);

		aSize.Width() = pActWin->GetTextWidth(aBulletStr);
		nWidth += pActWin->LogicToPixel( aSize ).Width();

		pActWin->SetFont( aOldFont );

		mnPageNumberWidthPixel = nWidth;
	}
	return mnPageNumberWidthPixel;
}
#endif

// --------------------------------------------------------------------

void OutlineView::UpdateParagraph( sal_uInt16 nPara )
{
	if( mpOutliner )
	{
	    SfxItemSet aNewAttrs2( mpOutliner->GetParaAttribs( nPara ) );
		aNewAttrs2.Put( maLRSpaceItem );
		mpOutliner->SetParaAttribs( nPara, aNewAttrs2 );
	}
}

// --------------------------------------------------------------------

void OutlineView::OnBeginPasteOrDrop( PasteOrDropInfos* /*pInfos*/ )
{
}

/** this is called after a paste or drop operation, make sure that the newly inserted paragraphs
	get the correct style sheet and new slides are inserted. */
void OutlineView::OnEndPasteOrDrop( PasteOrDropInfos* pInfos )
{
	SdPage* pPage = 0;
	SfxStyleSheetBasePool* pStylePool = GetDoc()->GetStyleSheetPool();

	for( sal_uInt16 nPara = pInfos->nStartPara; nPara <= pInfos->nEndPara; nPara++ )
	{
		Paragraph* pPara = mpOutliner->GetParagraph( nPara );

		bool bPage = mpOutliner->HasParaFlag( pPara, PARAFLAG_ISPAGE  );

		if( !bPage )
		{
			SdStyleSheet* pStyleSheet = dynamic_cast< SdStyleSheet* >( mpOutliner->GetStyleSheet( nPara ) );
			if( pStyleSheet )
			{
				const OUString aName( pStyleSheet->GetApiName() );
				if( aName.equalsAsciiL( RTL_CONSTASCII_STRINGPARAM("title" ) ) )
					bPage = true;
			}
		}

		if( !pPara )
			continue; // fatality!?

		if( bPage && (nPara != pInfos->nStartPara) )
		{
			// insert new slide for this paragraph
			pPage = InsertSlideForParagraph( pPara );
		}
		else
		{
			// newly inserted non page paragraphs get the outline style
			if( !pPage )
				pPage = GetPageForParagraph( pPara );

			if( pPage )
			{
				SfxStyleSheet* pStyle = pPage->GetStyleSheetForPresObj( bPage ? PRESOBJ_TITLE : PRESOBJ_OUTLINE );

				if( !bPage )
				{
					const sal_Int16 nDepth = mpOutliner->GetDepth( nPara );
					if( nDepth > 0 )
					{
						String aStyleSheetName( pStyle->GetName() );
						aStyleSheetName.Erase( aStyleSheetName.Len() - 1, 1 );
						aStyleSheetName += String::CreateFromInt32( nDepth );
						pStyle = static_cast<SfxStyleSheet*>( pStylePool->Find( aStyleSheetName, pStyle->GetFamily() ) );
						DBG_ASSERT( pStyle, "sd::OutlineView::OnEndPasteOrDrop(), Style not found!" );
					}
				}

				mpOutliner->SetStyleSheet( nPara, pStyle );
			}

			UpdateParagraph( nPara );
		}
	}
}

// ====================================================================


OutlineViewModelChangeGuard::OutlineViewModelChangeGuard( OutlineView& rView )
: mrView( rView )
{
	mrView.BeginModelChange();
}

OutlineViewModelChangeGuard::~OutlineViewModelChangeGuard()
{
	mrView.EndModelChange();
}

OutlineViewPageChangesGuard::OutlineViewPageChangesGuard( OutlineView* pView )
: mpView( pView )
{
	if( mpView )
		mpView->IgnoreCurrentPageChanges( true );
}

OutlineViewPageChangesGuard::~OutlineViewPageChangesGuard()
{
	if( mpView )
		mpView->IgnoreCurrentPageChanges( false );
}


} // end of namespace sd
