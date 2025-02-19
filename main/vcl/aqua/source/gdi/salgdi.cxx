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
#include "precompiled_vcl.hxx"

#include "osl/file.hxx"
#include "osl/process.h"

#include "vos/mutex.hxx"

#include "rtl/bootstrap.h"
#include "rtl/strbuf.hxx"

#include "basegfx/range/b2drectangle.hxx"
#include "basegfx/polygon/b2dpolygon.hxx"
#include "basegfx/polygon/b2dpolygontools.hxx"
#include "basegfx/matrix/b2dhommatrix.hxx"
#include "basegfx/matrix/b2dhommatrixtools.hxx"

#include "vcl/sysdata.hxx"
#include "vcl/svapp.hxx"

#include "aqua/salconst.h"
#include "aqua/salgdi.h"
#include "aqua/salbmp.h"
#include "aqua/salframe.h"
#include "aqua/salcolorutils.hxx"
#ifdef USE_ATSU
#include "atsfonts.hxx"
#else // !USE_ATSU
#include "ctfonts.hxx"
#endif

#include "fontsubset.hxx"
#include "impfont.hxx"
#include "sallayout.hxx"
#include "sft.hxx"

using namespace vcl;

// =======================================================================

SystemFontList::~SystemFontList( void )
{}

// =======================================================================

ImplMacTextStyle::ImplMacTextStyle( const ImplFontSelectData& rReqFont )
:	mpFontData( (ImplMacFontData*)rReqFont.mpFontData )
,	mfFontScale( 1.0 )
,	mfFontStretch( 1.0 )
,	mfFontRotation( 0.0 )
{}

// -----------------------------------------------------------------------

ImplMacTextStyle::~ImplMacTextStyle( void )
{}

// =======================================================================

ImplMacFontData::ImplMacFontData( const ImplDevFontAttributes& rDFA, sal_IntPtr nFontId )
:	ImplFontData( rDFA, 0 )
,	mnFontId( nFontId )
,	mpCharMap( NULL )
,	mbOs2Read( false )
,	mbHasOs2Table( false )
,	mbCmapEncodingRead( false )
,	mbHasCJKSupport( false )
{}

// -----------------------------------------------------------------------

ImplMacFontData::ImplMacFontData( const ImplMacFontData& rSrc )
:	ImplFontData(       rSrc)
,	mnFontId(           rSrc.mnFontId)
,	mpCharMap(          rSrc.mpCharMap)
,	mbOs2Read(          rSrc.mbOs2Read)
,	mbHasOs2Table(      rSrc.mbHasOs2Table)
,	mbCmapEncodingRead( rSrc.mbCmapEncodingRead)
,	mbHasCJKSupport(    rSrc.mbHasCJKSupport)
{
	if( mpCharMap )
		mpCharMap->AddReference();
}

// -----------------------------------------------------------------------

ImplMacFontData::~ImplMacFontData()
{
	if( mpCharMap )
		mpCharMap->DeReference();
}

// -----------------------------------------------------------------------

sal_IntPtr ImplMacFontData::GetFontId() const
{
    return reinterpret_cast<sal_IntPtr>( mnFontId);
}

// -----------------------------------------------------------------------

ImplFontEntry* ImplMacFontData::CreateFontInstance(ImplFontSelectData& rFSD) const
{
	return new ImplFontEntry(rFSD);
}

// -----------------------------------------------------------------------

static unsigned GetUShort( const unsigned char* p ){return((p[0]<<8)+p[1]);}
static unsigned GetUInt( const unsigned char* p ) { return((p[0]<<24)+(p[1]<<16)+(p[2]<<8)+p[3]);}

const ImplFontCharMap* ImplMacFontData::GetImplFontCharMap() const
{
	// return the cached charmap
	if( mpCharMap )
		return mpCharMap;

	// set the default charmap
	mpCharMap = ImplFontCharMap::GetDefaultMap();
	mpCharMap->AddReference();

	// get the CMAP byte size
	// allocate a buffer for the CMAP raw data
	const int nBufSize = GetFontTable( "cmap", NULL );
	DBG_ASSERT( (nBufSize > 0), "ImplMacFontData::GetImplFontCharMap : FontGetTable1 failed!\n");
	if( nBufSize <= 0 )
		return mpCharMap;

	// get the CMAP raw data
	ByteVector aBuffer( nBufSize );
	const int nRawSize = GetFontTable( "cmap", &aBuffer[0] );
	DBG_ASSERT( (nRawSize > 0), "ImplMacFontData::GetImplFontCharMap : ATSFontGetTable2 failed!\n");
	if( nRawSize <= 0 )
		return mpCharMap;
	DBG_ASSERT( (nBufSize==nRawSize), "ImplMacFontData::GetImplFontCharMap : ByteCount mismatch!\n");

	// parse the CMAP
	CmapResult aCmapResult;
	if( ParseCMAP( &aBuffer[0], nRawSize, aCmapResult ) )
	{
		// create the matching charmap
		mpCharMap->DeReference();
		mpCharMap = new ImplFontCharMap( aCmapResult );
		mpCharMap->AddReference();
	}

	return mpCharMap;
}

// -----------------------------------------------------------------------

void ImplMacFontData::ReadOs2Table( void ) const
{
	// read this only once per font
	if( mbOs2Read )
		return;
	mbOs2Read = true;
	mbHasOs2Table = false;

	// prepare to get the OS/2 table raw data
	const int nBufSize = GetFontTable( "OS/2", NULL );
	DBG_ASSERT( (nBufSize > 0), "ImplMacFontData::ReadOs2Table : FontGetTable1 failed!\n");
	if( nBufSize <= 0 )
		return;

	// get the OS/2 raw data
	ByteVector aBuffer( nBufSize );
	const int nRawSize = GetFontTable( "cmap", &aBuffer[0] );
	DBG_ASSERT( (nRawSize > 0), "ImplMacFontData::ReadOs2Table : ATSFontGetTable2 failed!\n");
	if( nRawSize <= 0 )
		return;
	DBG_ASSERT( (nBufSize == nRawSize), "ImplMacFontData::ReadOs2Table : ByteCount mismatch!\n");
	mbHasOs2Table = true;

	// parse the OS/2 raw data
	// TODO: also analyze panose info, etc.

	// check if the fonts needs the "CJK extra leading" heuristic
	const unsigned char* pOS2map = &aBuffer[0];
	const sal_uInt32 nVersion = GetUShort( pOS2map );
	if( nVersion >= 0x0001 )
	{
		const sal_uInt32 ulUnicodeRange2 = GetUInt( pOS2map + 46 );
		if( ulUnicodeRange2 & 0x2DF00000 )
			mbHasCJKSupport = true;
	}
}

void ImplMacFontData::ReadMacCmapEncoding( void ) const
{
	// read this only once per font
	if( mbCmapEncodingRead )
		return;
	mbCmapEncodingRead = true;

	const int nBufSize = GetFontTable( "cmap", NULL );
	if( nBufSize <= 0 )
		return;

	// get the CMAP raw data
	ByteVector aBuffer( nBufSize );
	const int nRawSize = GetFontTable( "cmap", &aBuffer[0] );
	if( nRawSize < 24 )
		return;

	const unsigned char* pCmap = &aBuffer[0];
	if( GetUShort( pCmap ) != 0x0000 )
		return;

	// check if the fonts needs the "CJK extra leading" heuristic
	int nSubTables = GetUShort( pCmap + 2 );
	for( const unsigned char* p = pCmap + 4; --nSubTables >= 0; p += 8 )
	{
		int nPlatform = GetUShort( p );
		if( nPlatform == kFontMacintoshPlatform ) {
			const int nEncoding = GetUShort (p + 2 );
			if( nEncoding == kFontJapaneseScript ||
			    nEncoding == kFontTraditionalChineseScript ||
			    nEncoding == kFontKoreanScript ||
			    nEncoding == kFontSimpleChineseScript )
			{
				mbHasCJKSupport = true;
				break;
			}
		}
	}
}

// -----------------------------------------------------------------------

bool ImplMacFontData::HasCJKSupport( void ) const
{
	ReadOs2Table();
	if( !mbHasOs2Table )
		ReadMacCmapEncoding();

	return mbHasCJKSupport;
}
		
// =======================================================================

AquaSalGraphics::AquaSalGraphics()
    : mpFrame( NULL )
    , mxLayer( NULL )
    , mrContext( NULL )
    , mpXorEmulation( NULL )
    , mnXorMode( 0 )
    , mnWidth( 0 )
    , mnHeight( 0 )
    , mnBitmapDepth( 0 )
    , mnRealDPIX( 0 )
    , mnRealDPIY( 0 )
    , mfFakeDPIScale( 1.0 )
    , mxClipPath( NULL )
    , maLineColor( COL_WHITE )
    , maFillColor( COL_BLACK )
    , mpMacFontData( NULL )
    , mpMacTextStyle( NULL )
    , maTextColor( COL_BLACK )
    , mbNonAntialiasedText( false )
    , mbPrinter( false )
    , mbVirDev( false )
    , mbWindow( false )
{}

// -----------------------------------------------------------------------

AquaSalGraphics::~AquaSalGraphics()
{
	CGPathRelease( mxClipPath );

    delete mpMacTextStyle;

	if( mpXorEmulation )
		delete mpXorEmulation;

	if( mxLayer )
		CGLayerRelease( mxLayer );
	else if( mrContext && mbWindow )
	{
		// destroy backbuffer bitmap context that we created ourself
		CGContextRelease( mrContext );
		mrContext = NULL;
		// memory is freed automatically by maOwnContextMemory
	}
}

bool AquaSalGraphics::supportsOperation( OutDevSupportType eType ) const
{
	bool bRet = false;
	switch( eType )
	{
		case OutDevSupport_TransparentRect:
		case OutDevSupport_B2DClip:
		case OutDevSupport_B2DDraw:
			bRet = true;
			break;
		default:
			break;
	}
	return bRet;
}

// =======================================================================

void AquaSalGraphics::updateResolution()
{
    DBG_ASSERT( mbWindow, "updateResolution on inappropriate graphics" );

    initResolution( (mbWindow && mpFrame) ? mpFrame->getNSWindow() : nil );
}

void AquaSalGraphics::initResolution( NSWindow* )
{
    // #i100617# read DPI only once; there is some kind of weird caching going on
    // if the main screen changes
    // FIXME: this is really unfortunate and needs to be investigated
    
    SalData* pSalData = GetSalData();
    if( pSalData->mnDPIX == 0 || pSalData->mnDPIY == 0 )
    {
        NSScreen* pScreen = nil;
        
        /* #i91301#
        many woes went into the try to have different resolutions
        on different screens. The result of these trials is that OOo is not ready
        for that yet, vcl and applications would need to be adapted.
        
        Unfortunately this is not possible in the 3.0 timeframe.
        So let's stay with one resolution for all Windows and VirtualDevices
        which is the resolution of the main screen
        
        This of course also means that measurements are exact only on the main screen.
        For activating different resolutions again just comment out the two lines below.
        
        if( pWin )
        pScreen = [pWin screen];
        */
        if( pScreen == nil )
        {
            NSArray* pScreens = [NSScreen screens];
            if( pScreens )
                pScreen = [pScreens objectAtIndex: 0];
        }
        
        mnRealDPIX = mnRealDPIY = 96;
        if( pScreen )
        {
            NSDictionary* pDev = [pScreen deviceDescription];
            if( pDev )
            {
                NSNumber* pVal = [pDev objectForKey: @"NSScreenNumber"];
                if( pVal )
                {
                    // FIXME: casting a long to CGDirectDisplayID is evil, but
                    // Apple suggest to do it this way
                    const CGDirectDisplayID nDisplayID = (CGDirectDisplayID)[pVal longValue];
                    const CGSize aSize = CGDisplayScreenSize( nDisplayID ); // => result is in millimeters
                    mnRealDPIX = static_cast<long>((CGDisplayPixelsWide( nDisplayID ) * 25.4) / aSize.width);
                    mnRealDPIY = static_cast<long>((CGDisplayPixelsHigh( nDisplayID ) * 25.4) / aSize.height);
                }
                else
                {
                    DBG_ERROR( "no resolution found in device description" );
                }
            }
            else
            {
                DBG_ERROR( "no device description" );
            }
        }
        else
        {
            DBG_ERROR( "no screen found" );
        }
        
        // #i107076# maintaining size-WYSIWYG-ness causes many problems for
        //           low-DPI, high-DPI or for mis-reporting devices
        //           => it is better to limit the calculation result then
        static const int nMinDPI = 72;
        if( (mnRealDPIX < nMinDPI) || (mnRealDPIY < nMinDPI) )
            mnRealDPIX = mnRealDPIY = nMinDPI;
        static const int nMaxDPI = 200;
        if( (mnRealDPIX > nMaxDPI) || (mnRealDPIY > nMaxDPI) )
            mnRealDPIX = mnRealDPIY = nMaxDPI;

        // for OSX any anisotropy reported for the display resolution is best ignored (e.g. TripleHead2Go)
        mnRealDPIX = mnRealDPIY = (mnRealDPIX + mnRealDPIY + 1) / 2;

        pSalData->mnDPIX = mnRealDPIX;
        pSalData->mnDPIY = mnRealDPIY;
    }
    else
    {
        mnRealDPIX = pSalData->mnDPIX;
        mnRealDPIY = pSalData->mnDPIY;
    }

    mfFakeDPIScale = 1.0;
}

void AquaSalGraphics::GetResolution( sal_Int32& rDPIX, sal_Int32& rDPIY )
{
    if( !mnRealDPIY )
        initResolution( (mbWindow && mpFrame) ? mpFrame->getNSWindow() : nil );

    rDPIX = lrint( mfFakeDPIScale * mnRealDPIX);
    rDPIY = lrint( mfFakeDPIScale * mnRealDPIY);
} 

void AquaSalGraphics::copyResolution( AquaSalGraphics& rGraphics )
{
	if( !rGraphics.mnRealDPIY && rGraphics.mbWindow && rGraphics.mpFrame )
		rGraphics.initResolution( rGraphics.mpFrame->getNSWindow() );

	mnRealDPIX = rGraphics.mnRealDPIX;
	mnRealDPIY = rGraphics.mnRealDPIY;
	mfFakeDPIScale = rGraphics.mfFakeDPIScale;
} 

// -----------------------------------------------------------------------

sal_uInt16 AquaSalGraphics::GetBitCount()
{
    sal_uInt16 nBits = mnBitmapDepth ? mnBitmapDepth : 32;//24;
    return nBits;
}

// -----------------------------------------------------------------------

static const basegfx::B2DPoint aHalfPointOfs ( 0.5, 0.5 );

static void AddPolygonToPath( CGMutablePathRef xPath,
	const ::basegfx::B2DPolygon& rPolygon, bool bClosePath, bool bPixelSnap, bool bLineDraw )
{
	// short circuit if there is nothing to do
	const int nPointCount = rPolygon.count();
	if( nPointCount <= 0 )
		return;

	(void)bPixelSnap; // TODO
	const CGAffineTransform* pTransform = NULL;

	const bool bHasCurves = rPolygon.areControlPointsUsed();
	for( int nPointIdx = 0, nPrevIdx = 0;; nPrevIdx = nPointIdx++ )
	{
		int nClosedIdx = nPointIdx;
		if( nPointIdx >= nPointCount )
		{
			// prepare to close last curve segment if needed
			if( bClosePath && (nPointIdx == nPointCount) )
				nClosedIdx = 0;
			else
				break;
		}

		::basegfx::B2DPoint aPoint = rPolygon.getB2DPoint( nClosedIdx );

		if( bPixelSnap)
		{
			// snap device coordinates to full pixels
			aPoint.setX( basegfx::fround( aPoint.getX() ) );
			aPoint.setY( basegfx::fround( aPoint.getY() ) );
		}

		if( bLineDraw )
			aPoint += aHalfPointOfs;

		if( !nPointIdx ) { // first point => just move there
			CGPathMoveToPoint( xPath, pTransform, aPoint.getX(), aPoint.getY() );
			continue;
		}

		bool bPendingCurve = false;
		if( bHasCurves )
		{
			bPendingCurve = rPolygon.isNextControlPointUsed( nPrevIdx );
			bPendingCurve |= rPolygon.isPrevControlPointUsed( nClosedIdx );
		}

		if( !bPendingCurve )	// line segment
			CGPathAddLineToPoint( xPath, pTransform, aPoint.getX(), aPoint.getY() );
		else						// cubic bezier segment
		{
			basegfx::B2DPoint aCP1 = rPolygon.getNextControlPoint( nPrevIdx );
			basegfx::B2DPoint aCP2 = rPolygon.getPrevControlPoint( nClosedIdx );
			if( bLineDraw )
			{
				aCP1 += aHalfPointOfs;
				aCP2 += aHalfPointOfs;
			}
			CGPathAddCurveToPoint( xPath, pTransform, aCP1.getX(), aCP1.getY(),
				aCP2.getX(), aCP2.getY(), aPoint.getX(), aPoint.getY() );
		}
	}

	if( bClosePath )
		CGPathCloseSubpath( xPath );
}

static void AddPolyPolygonToPath( CGMutablePathRef xPath,
	const ::basegfx::B2DPolyPolygon& rPolyPoly, bool bPixelSnap, bool bLineDraw )
{
	// short circuit if there is nothing to do
	const int nPolyCount = rPolyPoly.count();
	if( nPolyCount <= 0 )
		return;

	for( int nPolyIdx = 0; nPolyIdx < nPolyCount; ++nPolyIdx )
	{
		const ::basegfx::B2DPolygon rPolygon = rPolyPoly.getB2DPolygon( nPolyIdx );
		AddPolygonToPath( xPath, rPolygon, true, bPixelSnap, bLineDraw );
	}
}

// -----------------------------------------------------------------------

void AquaSalGraphics::ResetClipRegion()
{
    // release old path and indicate no clipping
    if( mxClipPath )
    {
        CGPathRelease( mxClipPath );
        mxClipPath = NULL;
    }
    if( CheckContext() )
        SetState();
}

// -----------------------------------------------------------------------

bool AquaSalGraphics::setClipRegion( const Region& i_rClip )
{
    // release old clip path
    if( mxClipPath )
    {
        CGPathRelease( mxClipPath );
        mxClipPath = NULL;
    }
    mxClipPath = CGPathCreateMutable();

    // set current path, either as polypolgon or sequence of rectangles
    if(i_rClip.HasPolyPolygonOrB2DPolyPolygon())
    {
        const basegfx::B2DPolyPolygon aClip(i_rClip.GetAsB2DPolyPolygon());

        AddPolyPolygonToPath( mxClipPath, aClip, !getAntiAliasB2DDraw(), false );
    }
    else
    {
        RectangleVector aRectangles;
        i_rClip.GetRegionRectangles(aRectangles);

        for(RectangleVector::const_iterator aRectIter(aRectangles.begin()); aRectIter != aRectangles.end(); aRectIter++)
        {
            const long nW(aRectIter->Right() - aRectIter->Left() + 1); // uses +1 logic in original

            if(nW)
            {
                const long nH(aRectIter->Bottom() - aRectIter->Top() + 1); // uses +1 logic in original

                if(nH)
                {
                    const CGRect aRect = CGRectMake( aRectIter->Left(), aRectIter->Top(), nW, nH);
                    CGPathAddRect( mxClipPath, NULL, aRect );
                }
            }
        }
    }
    // set the current path as clip region
	if( CheckContext() )
	    SetState();
	return true;
}

// -----------------------------------------------------------------------

void AquaSalGraphics::SetLineColor()
{
    maLineColor.SetAlpha( 0.0 );   // transparent
    if( CheckContext() )
        CGContextSetStrokeColor( mrContext, maLineColor.AsArray() );
} 

// -----------------------------------------------------------------------

void AquaSalGraphics::SetLineColor( SalColor nSalColor )
{
	maLineColor = RGBAColor( nSalColor );
    if( CheckContext() )
        CGContextSetStrokeColor( mrContext, maLineColor.AsArray() );
}

// -----------------------------------------------------------------------

void AquaSalGraphics::SetFillColor()
{
    maFillColor.SetAlpha( 0.0 );   // transparent
    if( CheckContext() )
        CGContextSetFillColor( mrContext, maFillColor.AsArray() );
}

// -----------------------------------------------------------------------

void AquaSalGraphics::SetFillColor( SalColor nSalColor )
{
	maFillColor = RGBAColor( nSalColor );
    if( CheckContext() )
        CGContextSetFillColor( mrContext, maFillColor.AsArray() );
} 

// -----------------------------------------------------------------------

static SalColor ImplGetROPSalColor( SalROPColor nROPColor )
{
	SalColor nSalColor;
	if ( nROPColor == SAL_ROP_0 )
		nSalColor = MAKE_SALCOLOR( 0, 0, 0 );
	else
		nSalColor = MAKE_SALCOLOR( 255, 255, 255 );
	return nSalColor;
}

void AquaSalGraphics::SetROPLineColor( SalROPColor nROPColor )
{
    if( ! mbPrinter )
        SetLineColor( ImplGetROPSalColor( nROPColor ) );
} 

// -----------------------------------------------------------------------

void AquaSalGraphics::SetROPFillColor( SalROPColor nROPColor )
{
    if( ! mbPrinter )
        SetFillColor( ImplGetROPSalColor( nROPColor ) );
}

// -----------------------------------------------------------------------

void AquaSalGraphics::ImplDrawPixel( long nX, long nY, const RGBAColor& rColor )
{
	if( !CheckContext() )
		return;

	// overwrite the fill color
	CGContextSetFillColor( mrContext, rColor.AsArray() );
	// draw 1x1 rect, there is no pixel drawing in Quartz
	const CGRect aDstRect = CGRectMake( nX, nY, 1, 1);
	CGContextFillRect( mrContext, aDstRect );
    RefreshRect( aDstRect );
    // reset the fill color
	CGContextSetFillColor( mrContext, maFillColor.AsArray() );
}

void AquaSalGraphics::drawPixel( long nX, long nY )
{
    // draw pixel with current line color
    ImplDrawPixel( nX, nY, maLineColor );
} 

void AquaSalGraphics::drawPixel( long nX, long nY, SalColor nSalColor )
{
	const RGBAColor aPixelColor( nSalColor );
    ImplDrawPixel( nX, nY, aPixelColor );
} 

// -----------------------------------------------------------------------

void AquaSalGraphics::drawLine( long nX1, long nY1, long nX2, long nY2 )
{
    if( nX1 == nX2 && nY1 == nY2 )
    {
        // #i109453# platform independent code expects at least one pixel to be drawn
        drawPixel( nX1, nY1 );
        return;
    }
    
	if( !CheckContext() )
		return;

	CGContextBeginPath( mrContext );
	CGContextMoveToPoint( mrContext, static_cast<float>(nX1)+0.5, static_cast<float>(nY1)+0.5 );
	CGContextAddLineToPoint( mrContext, static_cast<float>(nX2)+0.5, static_cast<float>(nY2)+0.5 );
	CGContextDrawPath( mrContext, kCGPathStroke );

    Rectangle aRefreshRect( nX1, nY1, nX2, nY2 );
}

// -----------------------------------------------------------------------

void AquaSalGraphics::drawRect( long nX, long nY, long nWidth, long nHeight )
{
	if( !CheckContext() )
		return;

	 CGRect aRect( CGRectMake(nX, nY, nWidth, nHeight) );
	 if( IsPenVisible() )
	 {
	     aRect.origin.x      += 0.5;
	     aRect.origin.y      += 0.5;
	     aRect.size.width    -= 1;
	     aRect.size.height -= 1;
	 }
	 
	 if( IsBrushVisible() )
	     CGContextFillRect( mrContext, aRect );
	
	 if( IsPenVisible() )
	     CGContextStrokeRect( mrContext, aRect );

	RefreshRect( nX, nY, nWidth, nHeight );
}

// -----------------------------------------------------------------------

static void getBoundRect( sal_uInt32 nPoints, const SalPoint* pPtAry, long &rX, long& rY, long& rWidth, long& rHeight )
{
    long nX1 = pPtAry->mnX;
    long nX2 = nX1;
    long nY1 = pPtAry->mnY;
    long nY2 = nY1;
    for( sal_uInt32 n = 1; n < nPoints; ++n )
    {
        if( pPtAry[n].mnX < nX1 )
            nX1 = pPtAry[n].mnX;
        else if( pPtAry[n].mnX > nX2 )
            nX2 = pPtAry[n].mnX;

        if( pPtAry[n].mnY < nY1 )
            nY1 = pPtAry[n].mnY;
        else if( pPtAry[n].mnY > nY2 )
            nY2 = pPtAry[n].mnY;
    }
    rX = nX1;
    rY = nY1;
    rWidth = nX2 - nX1 + 1;
    rHeight = nY2 - nY1 + 1;
}

static inline void alignLinePoint( const SalPoint* i_pIn, float& o_fX, float& o_fY )
{
    o_fX = static_cast<float>(i_pIn->mnX ) + 0.5;
    o_fY = static_cast<float>(i_pIn->mnY ) + 0.5;
}

void AquaSalGraphics::drawPolyLine( sal_uInt32 nPoints, const SalPoint* pPtAry )
{
	if( nPoints < 1 )
		return;
	if( !CheckContext() )
		return;

	long nX = 0, nY = 0, nWidth = 0, nHeight = 0;
	getBoundRect( nPoints, pPtAry, nX, nY, nWidth, nHeight );
	
	float fX, fY;
	
	CGContextBeginPath( mrContext );
	alignLinePoint( pPtAry, fX, fY );
	CGContextMoveToPoint( mrContext, fX, fY );
	pPtAry++;
	for( sal_uInt32 nPoint = 1; nPoint < nPoints; ++nPoint, ++pPtAry )
	{
	    alignLinePoint( pPtAry, fX, fY );
	    CGContextAddLineToPoint( mrContext, fX, fY );
	}
	CGContextDrawPath( mrContext, kCGPathStroke );

	RefreshRect( nX, nY, nWidth, nHeight );        
}

// -----------------------------------------------------------------------

void AquaSalGraphics::drawPolygon( sal_uInt32 nPoints, const SalPoint* pPtAry )
{
	if( nPoints <= 1 )
		return;
	if( !CheckContext() )
		return;

	long nX = 0, nY = 0, nWidth = 0, nHeight = 0;
	getBoundRect( nPoints, pPtAry, nX, nY, nWidth, nHeight );

    CGPathDrawingMode eMode;
    if( IsBrushVisible() && IsPenVisible() )
        eMode = kCGPathEOFillStroke;
    else if( IsPenVisible() )
        eMode = kCGPathStroke;
    else if( IsBrushVisible() )
        eMode = kCGPathEOFill;
    else
        return;

	CGContextBeginPath( mrContext );
    
    if( IsPenVisible() )
    {
        float fX, fY;
        alignLinePoint( pPtAry, fX, fY );
        CGContextMoveToPoint( mrContext, fX, fY );
        pPtAry++;
        for( sal_uInt32 nPoint = 1; nPoint < nPoints; ++nPoint, ++pPtAry )
        {
            alignLinePoint( pPtAry, fX, fY );
            CGContextAddLineToPoint( mrContext, fX, fY );
        }
    }
    else
    {
        CGContextMoveToPoint( mrContext, pPtAry->mnX, pPtAry->mnY );
        pPtAry++;
        for( sal_uInt32 nPoint = 1; nPoint < nPoints; ++nPoint, ++pPtAry )
            CGContextAddLineToPoint( mrContext, pPtAry->mnX, pPtAry->mnY );
    }

    CGContextDrawPath( mrContext, eMode );
	RefreshRect( nX, nY, nWidth, nHeight );        
}

// -----------------------------------------------------------------------

void AquaSalGraphics::drawPolyPolygon( sal_uInt32 nPolyCount, const sal_uInt32* pPoints, PCONSTSALPOINT* ppPtAry )
{
    if( nPolyCount <= 0 )
		return;
    if( !CheckContext() )
		return;

	// find bound rect
	long leftX = 0, topY = 0, maxWidth = 0, maxHeight = 0;
	getBoundRect( pPoints[0], ppPtAry[0], leftX, topY, maxWidth, maxHeight );
	for( sal_uLong n = 1; n < nPolyCount; n++ )
	{
	    long nX = leftX, nY = topY, nW = maxWidth, nH = maxHeight;
	    getBoundRect( pPoints[n], ppPtAry[n], nX, nY, nW, nH );
	    if( nX < leftX )
	    {
	        maxWidth += leftX - nX;
	        leftX = nX;
	    }
	    if( nY < topY )
	    {
	        maxHeight += topY - nY;
	        topY = nY;
	    }
	    if( nX + nW > leftX + maxWidth )
	        maxWidth = nX + nW - leftX;
	    if( nY + nH > topY + maxHeight )
	        maxHeight = nY + nH - topY;
	}

	// prepare drawing mode
	CGPathDrawingMode eMode;
	if( IsBrushVisible() && IsPenVisible() )
	    eMode = kCGPathEOFillStroke;
	else if( IsPenVisible() )
	    eMode = kCGPathStroke;
	else if( IsBrushVisible() )
	    eMode = kCGPathEOFill;
	else
	    return;

	// convert to CGPath
	CGContextBeginPath( mrContext );
	if( IsPenVisible() )
	{
	    for( sal_uLong nPoly = 0; nPoly < nPolyCount; nPoly++ )
	    {
	        const sal_uInt32 nPoints = pPoints[nPoly];
	        if( nPoints > 1 )
	        {
	            const SalPoint *pPtAry = ppPtAry[nPoly];
	            float fX, fY;
	            alignLinePoint( pPtAry, fX, fY );
	            CGContextMoveToPoint( mrContext, fX, fY );
	            pPtAry++;
	            for( sal_uInt32 nPoint = 1; nPoint < nPoints; ++nPoint, ++pPtAry )
	            {
	                alignLinePoint( pPtAry, fX, fY );
	                CGContextAddLineToPoint( mrContext, fX, fY );
	            }
	            CGContextClosePath(mrContext);
	        }
	    }
	}
	else
	{
	    for( sal_uLong nPoly = 0; nPoly < nPolyCount; nPoly++ )
	    {
	        const sal_uInt32 nPoints = pPoints[nPoly];
	        if( nPoints > 1 )
	        {
	            const SalPoint *pPtAry = ppPtAry[nPoly];
	            CGContextMoveToPoint( mrContext, pPtAry->mnX, pPtAry->mnY );
	            pPtAry++;
	            for( sal_uInt32 nPoint = 1; nPoint < nPoints; ++nPoint, ++pPtAry )
	                CGContextAddLineToPoint( mrContext, pPtAry->mnX, pPtAry->mnY );
	            CGContextClosePath(mrContext);
	        }
	    }
	}
	
	CGContextDrawPath( mrContext, eMode );
        
	RefreshRect( leftX, topY, maxWidth, maxHeight );
} 

// -----------------------------------------------------------------------

bool AquaSalGraphics::drawPolyPolygon( const ::basegfx::B2DPolyPolygon& rPolyPoly,
	double fTransparency )
{
	// short circuit if there is nothing to do
	const int nPolyCount = rPolyPoly.count();
	if( nPolyCount <= 0 )
		return true;

	// ignore invisible polygons
	if( (fTransparency >= 1.0) || (fTransparency < 0) )
		return true;

	// setup poly-polygon path
	CGMutablePathRef xPath = CGPathCreateMutable();
	for( int nPolyIdx = 0; nPolyIdx < nPolyCount; ++nPolyIdx )
	{
		const ::basegfx::B2DPolygon rPolygon = rPolyPoly.getB2DPolygon( nPolyIdx );
		AddPolygonToPath( xPath, rPolygon, true, !getAntiAliasB2DDraw(), IsPenVisible() );
	}

	const CGRect aRefreshRect = CGPathGetBoundingBox( xPath );
#ifndef NO_I97317_WORKAROUND
	// #i97317# workaround for Quartz having problems with drawing small polygons
	if( ! ((aRefreshRect.size.width <= 0.125) && (aRefreshRect.size.height <= 0.125)) )
#endif
    {
        // use the path to prepare the graphics context
        CGContextSaveGState( mrContext );
        CGContextBeginPath( mrContext );
        CGContextAddPath( mrContext, xPath );
    
        // draw path with antialiased polygon
        CGContextSetShouldAntialias( mrContext, true );
        CGContextSetAlpha( mrContext, 1.0 - fTransparency ); 
        CGContextDrawPath( mrContext, kCGPathEOFillStroke );
        CGContextRestoreGState( mrContext );
    
        // mark modified rectangle as updated
        RefreshRect( aRefreshRect );
    }        

	CGPathRelease( xPath );

	return true;
}

// -----------------------------------------------------------------------

bool AquaSalGraphics::drawPolyLine( 
    const ::basegfx::B2DPolygon& rPolyLine,
	double fTransparency,
	const ::basegfx::B2DVector& rLineWidths,
	basegfx::B2DLineJoin eLineJoin,
    com::sun::star::drawing::LineCap eLineCap)
{
	// short circuit if there is nothing to do
	const int nPointCount = rPolyLine.count();
	if( nPointCount <= 0 )
		return true;

	// reject requests that cannot be handled yet
	if( rLineWidths.getX() != rLineWidths.getY() )
		return false;

	// #i101491# Aqua does not support B2DLINEJOIN_NONE; return false to use
	// the fallback (own geometry preparation)
	// #i104886# linejoin-mode and thus the above only applies to "fat" lines
	if( (basegfx::B2DLINEJOIN_NONE == eLineJoin)
	&& (rLineWidths.getX() > 1.3) )
		return false;

	// setup line attributes
	CGLineJoin aCGLineJoin = kCGLineJoinMiter;
	switch( eLineJoin ) {
		case ::basegfx::B2DLINEJOIN_NONE:		aCGLineJoin = /*TODO?*/kCGLineJoinMiter; break;
		case ::basegfx::B2DLINEJOIN_MIDDLE:		aCGLineJoin = /*TODO?*/kCGLineJoinMiter; break;
		case ::basegfx::B2DLINEJOIN_BEVEL:		aCGLineJoin = kCGLineJoinBevel; break;
		case ::basegfx::B2DLINEJOIN_MITER:		aCGLineJoin = kCGLineJoinMiter; break;
		case ::basegfx::B2DLINEJOIN_ROUND:		aCGLineJoin = kCGLineJoinRound; break;
	}

    // setup cap attribute
    CGLineCap aCGLineCap(kCGLineCapButt);

    switch(eLineCap)
    {
        default: // com::sun::star::drawing::LineCap_BUTT:
        {
            aCGLineCap = kCGLineCapButt;
            break;
        }
        case com::sun::star::drawing::LineCap_ROUND:
        {
            aCGLineCap = kCGLineCapRound;
            break;
        }
        case com::sun::star::drawing::LineCap_SQUARE:
        {
            aCGLineCap = kCGLineCapSquare;
            break;
        }
    }

	// setup poly-polygon path
	CGMutablePathRef xPath = CGPathCreateMutable();
	AddPolygonToPath( xPath, rPolyLine, rPolyLine.isClosed(), !getAntiAliasB2DDraw(), true );

	const CGRect aRefreshRect = CGPathGetBoundingBox( xPath );
#ifndef NO_I97317_WORKAROUND
	// #i97317# workaround for Quartz having problems with drawing small polygons
	if( ! ((aRefreshRect.size.width <= 0.125) && (aRefreshRect.size.height <= 0.125)) )
#endif
    {	
        // use the path to prepare the graphics context
        CGContextSaveGState( mrContext );
        CGContextAddPath( mrContext, xPath );
        // draw path with antialiased line
        CGContextSetShouldAntialias( mrContext, true );
        CGContextSetAlpha( mrContext, 1.0 - fTransparency ); 
        CGContextSetLineJoin( mrContext, aCGLineJoin );
        CGContextSetLineCap( mrContext, aCGLineCap );
        CGContextSetLineWidth( mrContext, rLineWidths.getX() );
        CGContextDrawPath( mrContext, kCGPathStroke );
        CGContextRestoreGState( mrContext );
        
        // mark modified rectangle as updated
        RefreshRect( aRefreshRect );
    }        

	CGPathRelease( xPath );

	return true;
}

// -----------------------------------------------------------------------

sal_Bool AquaSalGraphics::drawPolyLineBezier( sal_uInt32, const SalPoint*, const sal_uInt8* )
{
    return sal_False;
}

// -----------------------------------------------------------------------

sal_Bool AquaSalGraphics::drawPolygonBezier( sal_uInt32, const SalPoint*, const sal_uInt8* )
{
    return sal_False;
}

// -----------------------------------------------------------------------

sal_Bool AquaSalGraphics::drawPolyPolygonBezier( sal_uInt32, const sal_uInt32*,
                                             const SalPoint* const*, const sal_uInt8* const* )
{
    return sal_False;
}

// -----------------------------------------------------------------------

void AquaSalGraphics::copyBits( const SalTwoRect& rPosAry, SalGraphics *pSrcGraphics )
{
	if( !pSrcGraphics )
		pSrcGraphics = this;

    //from unix salgdi2.cxx
    //[FIXME] find a better way to prevent calc from crashing when width and height are negative
    if( rPosAry.mnSrcWidth <= 0
        || rPosAry.mnSrcHeight <= 0
        || rPosAry.mnDestWidth <= 0
        || rPosAry.mnDestHeight <= 0 )
    {
        return;
    }

	// accelerate trivial operations
	/*const*/ AquaSalGraphics* pSrc = static_cast<AquaSalGraphics*>(pSrcGraphics);
	const bool bSameGraphics = (this == pSrc) || (mbWindow && mpFrame && pSrc->mbWindow && (mpFrame == pSrc->mpFrame));
	if( bSameGraphics
	&&  (rPosAry.mnSrcWidth == rPosAry.mnDestWidth)
	&&  (rPosAry.mnSrcHeight == rPosAry.mnDestHeight))
	{
		// short circuit if there is nothing to do
		if( (rPosAry.mnSrcX == rPosAry.mnDestX)
		&&  (rPosAry.mnSrcY == rPosAry.mnDestY))
			return;
		// use copyArea() if source and destination context are identical
		copyArea( rPosAry.mnDestX, rPosAry.mnDestY, rPosAry.mnSrcX, rPosAry.mnSrcY,
			rPosAry.mnSrcWidth, rPosAry.mnSrcHeight, 0 );
		return;
	}

	ApplyXorContext();
	pSrc->ApplyXorContext();

	DBG_ASSERT( pSrc->mxLayer!=NULL, "AquaSalGraphics::copyBits() from non-layered graphics" );

	const CGPoint aDstPoint = CGPointMake( +rPosAry.mnDestX - rPosAry.mnSrcX, rPosAry.mnDestY - rPosAry.mnSrcY);
	if( (rPosAry.mnSrcWidth == rPosAry.mnDestWidth && rPosAry.mnSrcHeight == rPosAry.mnDestHeight) &&
	    (!mnBitmapDepth || (aDstPoint.x + pSrc->mnWidth) <= mnWidth) ) // workaround a Quartz crasher
    {
	    // in XOR mode the drawing context is redirected to the XOR mask
	    // if source and target are identical then copyBits() paints onto the target context though 
	    CGContextRef xCopyContext = mrContext;
	    if( mpXorEmulation && mpXorEmulation->IsEnabled() )
		    if( pSrcGraphics == this )
			    xCopyContext = mpXorEmulation->GetTargetContext();

	    CGContextSaveGState( xCopyContext );
	    const CGRect aDstRect = CGRectMake( rPosAry.mnDestX, rPosAry.mnDestY, rPosAry.mnDestWidth, rPosAry.mnDestHeight);
	    CGContextClipToRect( xCopyContext, aDstRect );

	    // draw at new destination
	    // NOTE: flipped drawing gets disabled for this, else the subimage would be drawn upside down
	    if( pSrc->IsFlipped() )
		    { CGContextTranslateCTM( xCopyContext, 0, +mnHeight ); CGContextScaleCTM( xCopyContext, +1, -1 ); }
	    // TODO: pSrc->size() != this->size()
		    ::CGContextDrawLayerAtPoint( xCopyContext, aDstPoint, pSrc->mxLayer );
	    CGContextRestoreGState( xCopyContext );
	    // mark the destination rectangle as updated
   	    RefreshRect( aDstRect );
    }
    else
    {
	    SalBitmap* pBitmap = pSrc->getBitmap( rPosAry.mnSrcX, rPosAry.mnSrcY, rPosAry.mnSrcWidth, rPosAry.mnSrcHeight );

	    if( pBitmap )
	    {
		    SalTwoRect aPosAry( rPosAry );
		    aPosAry.mnSrcX = 0;
		    aPosAry.mnSrcY = 0;
		    drawBitmap( aPosAry, *pBitmap );
		    delete pBitmap;
	    }
    }
}

// -----------------------------------------------------------------------

void AquaSalGraphics::copyArea( long nDstX, long nDstY,long nSrcX, long nSrcY, long nSrcWidth, long nSrcHeight, sal_uInt16 /*nFlags*/ )
{
	ApplyXorContext();

#if 0 // TODO: make AquaSalBitmap as fast as the alternative implementation below
	SalBitmap* pBitmap = getBitmap( nSrcX, nSrcY, nSrcWidth, nSrcHeight );
	if( pBitmap )
	{
		SalTwoRect aPosAry;
		aPosAry.mnSrcX = 0;
		aPosAry.mnSrcY = 0;
		aPosAry.mnSrcWidth = nSrcWidth;
		aPosAry.mnSrcHeight = nSrcHeight;
		aPosAry.mnDestX = nDstX;
		aPosAry.mnDestY = nDstY;
		aPosAry.mnDestWidth = nSrcWidth;
		aPosAry.mnDestHeight = nSrcHeight;
		drawBitmap( aPosAry, *pBitmap );
		delete pBitmap;
	}
#else
	DBG_ASSERT( mxLayer!=NULL, "AquaSalGraphics::copyArea() for non-layered graphics" );

	// in XOR mode the drawing context is redirected to the XOR mask
	// copyArea() always works on the target context though
	CGContextRef xCopyContext = mrContext;
	if( mpXorEmulation && mpXorEmulation->IsEnabled() )
		xCopyContext = mpXorEmulation->GetTargetContext();

	// drawing a layer onto its own context causes trouble on OSX => copy it first
	// TODO: is it possible to get rid of this unneeded copy more often?
	//       e.g. on OSX>=10.5 only this situation causes problems:
	//			mnBitmapDepth && (aDstPoint.x + pSrc->mnWidth) > mnWidth
	CGLayerRef xSrcLayer = mxLayer;
	// TODO: if( mnBitmapDepth > 0 )
	{
		const CGSize aSrcSize = CGSizeMake( nSrcWidth, nSrcHeight);
		xSrcLayer = ::CGLayerCreateWithContext( xCopyContext, aSrcSize, NULL );
		const CGContextRef xSrcContext = CGLayerGetContext( xSrcLayer );
		CGPoint aSrcPoint = CGPointMake( -nSrcX, -nSrcY);
		if( IsFlipped() )
		{
			::CGContextTranslateCTM( xSrcContext, 0, +nSrcHeight );
			::CGContextScaleCTM( xSrcContext, +1, -1 );
			aSrcPoint.y = (nSrcY + nSrcHeight) - mnHeight;
		}
		::CGContextDrawLayerAtPoint( xSrcContext, aSrcPoint, mxLayer );
	}

	// draw at new destination
	const CGPoint aDstPoint = CGPointMake( +nDstX, +nDstY);
	::CGContextDrawLayerAtPoint( xCopyContext, aDstPoint, xSrcLayer );

	// cleanup
	if( xSrcLayer != mxLayer )
		CGLayerRelease( xSrcLayer );

	// mark the destination rectangle as updated
    RefreshRect( nDstX, nDstY, nSrcWidth, nSrcHeight );
#endif
}

// -----------------------------------------------------------------------

void AquaSalGraphics::drawBitmap( const SalTwoRect& rPosAry, const SalBitmap& rSalBitmap )
{
	if( !CheckContext() )
		return;

	const AquaSalBitmap& rBitmap = static_cast<const AquaSalBitmap&>(rSalBitmap);
	CGImageRef xImage = rBitmap.CreateCroppedImage( (int)rPosAry.mnSrcX, (int)rPosAry.mnSrcY, (int)rPosAry.mnSrcWidth, (int)rPosAry.mnSrcHeight );
	if( !xImage )
		return;

	const CGRect aDstRect = CGRectMake( rPosAry.mnDestX, rPosAry.mnDestY, rPosAry.mnDestWidth, rPosAry.mnDestHeight);
	CGContextDrawImage( mrContext, aDstRect, xImage );
	CGImageRelease( xImage );
	RefreshRect( aDstRect );
}

// -----------------------------------------------------------------------

void AquaSalGraphics::drawBitmap( const SalTwoRect& rPosAry, const SalBitmap& rSalBitmap,SalColor )
{
	DBG_ERROR( "not implemented for color masking!");
	drawBitmap( rPosAry, rSalBitmap );
}

// -----------------------------------------------------------------------

void AquaSalGraphics::drawBitmap( const SalTwoRect& rPosAry, const SalBitmap& rSalBitmap, const SalBitmap& rTransparentBitmap )
{
	if( !CheckContext() )
		return;

	const AquaSalBitmap& rBitmap = static_cast<const AquaSalBitmap&>(rSalBitmap);
	const AquaSalBitmap& rMask = static_cast<const AquaSalBitmap&>(rTransparentBitmap);
	CGImageRef xMaskedImage( rBitmap.CreateWithMask( rMask, rPosAry.mnSrcX, rPosAry.mnSrcY, rPosAry.mnSrcWidth, rPosAry.mnSrcHeight ) );
	if( !xMaskedImage )
		return;

	const CGRect aDstRect = CGRectMake( rPosAry.mnDestX, rPosAry.mnDestY, rPosAry.mnDestWidth, rPosAry.mnDestHeight);
	CGContextDrawImage( mrContext, aDstRect, xMaskedImage );
	CGImageRelease( xMaskedImage );
	RefreshRect( aDstRect );
}

// -----------------------------------------------------------------------

void AquaSalGraphics::drawMask( const SalTwoRect& rPosAry, const SalBitmap& rSalBitmap, SalColor nMaskColor )
{
	if( !CheckContext() )
		return;

	const AquaSalBitmap& rBitmap = static_cast<const AquaSalBitmap&>(rSalBitmap);
	CGImageRef xImage = rBitmap.CreateColorMask( rPosAry.mnSrcX, rPosAry.mnSrcY, rPosAry.mnSrcWidth, rPosAry.mnSrcHeight, nMaskColor );
	if( !xImage )
		return;

	const CGRect aDstRect = CGRectMake( rPosAry.mnDestX, rPosAry.mnDestY, rPosAry.mnDestWidth, rPosAry.mnDestHeight);
	CGContextDrawImage( mrContext, aDstRect, xImage );
	CGImageRelease( xImage );
	RefreshRect( aDstRect );
}

// -----------------------------------------------------------------------

SalBitmap* AquaSalGraphics::getBitmap( long  nX, long  nY, long  nDX, long  nDY )
{
	DBG_ASSERT( mxLayer, "AquaSalGraphics::getBitmap() with no layer" );

	ApplyXorContext();

	AquaSalBitmap* pBitmap = new AquaSalBitmap;
	if( !pBitmap->Create( mxLayer, mnBitmapDepth, nX, nY, nDX, nDY, !mbWindow ) )
	{
		delete pBitmap;
		pBitmap = NULL;
	}

	return pBitmap;
}

// -----------------------------------------------------------------------

SalColor AquaSalGraphics::getPixel( long nX, long nY )
{
	// return default value on printers or when out of bounds
	if( !mxLayer
	|| (nX < 0) || (nX >= mnWidth)
	|| (nY < 0) || (nY >= mnHeight))
		return COL_BLACK;

	// prepare creation of matching a CGBitmapContext
	CGColorSpaceRef aCGColorSpace = GetSalData()->mxRGBSpace;
	CGBitmapInfo aCGBmpInfo = kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Big;
#if __BIG_ENDIAN__
	struct{ unsigned char b, g, r, a; } aPixel;
#else
	struct{ unsigned char a, r, g, b; } aPixel;
#endif

	// create a one-pixel bitmap context
	// TODO: is it worth to cache it?
	CGContextRef xOnePixelContext = ::CGBitmapContextCreate( &aPixel,
		1, 1, 8, sizeof(aPixel), aCGColorSpace, aCGBmpInfo );

	// update this graphics layer
	ApplyXorContext();

	// copy the requested pixel into the bitmap context
	if( IsFlipped() )
		nY = mnHeight - nY;
	const CGPoint aCGPoint = CGPointMake( -nX, -nY);
	CGContextDrawLayerAtPoint( xOnePixelContext, aCGPoint, mxLayer );
	CGContextRelease( xOnePixelContext );

	SalColor nSalColor = MAKE_SALCOLOR( aPixel.r, aPixel.g, aPixel.b );
	return nSalColor;
}

// -----------------------------------------------------------------------


static void DrawPattern50( void*, CGContextRef rContext )
{
    static const CGRect aRects[2] = { { {0,0}, { 2, 2 } }, { { 2, 2 }, { 2, 2 } } };
    CGContextAddRects( rContext, aRects, 2 );
    CGContextFillPath( rContext );
}

void AquaSalGraphics::Pattern50Fill()
{
    static const CGFloat aFillCol[4] = { 1,1,1,1 };
    static const CGPatternCallbacks aCallback = { 0, &DrawPattern50, NULL };
    if( ! GetSalData()->mxP50Space )
        GetSalData()->mxP50Space = CGColorSpaceCreatePattern( GetSalData()->mxRGBSpace );
    if( ! GetSalData()->mxP50Pattern )
        GetSalData()->mxP50Pattern = CGPatternCreate( NULL, CGRectMake( 0, 0, 4, 4 ),
                                                      CGAffineTransformIdentity, 4, 4,
                                                      kCGPatternTilingConstantSpacing,
                                                      false, &aCallback );
        
    CGContextSetFillColorSpace( mrContext, GetSalData()->mxP50Space );
    CGContextSetFillPattern( mrContext, GetSalData()->mxP50Pattern, aFillCol );
    CGContextFillPath( mrContext );
}

void AquaSalGraphics::invert( long nX, long nY, long nWidth, long nHeight, SalInvert nFlags )
{
    if ( CheckContext() )
    {
        CGRect aCGRect = CGRectMake( nX, nY, nWidth, nHeight);
        CGContextSaveGState(mrContext);

        if ( nFlags & SAL_INVERT_TRACKFRAME )
        {
            const CGFloat dashLengths[2]  = { 4.0, 4.0 };     // for drawing dashed line
            CGContextSetBlendMode( mrContext, kCGBlendModeDifference );
            CGContextSetRGBStrokeColor ( mrContext, 1.0, 1.0, 1.0, 1.0 );
            CGContextSetLineDash ( mrContext, 0, dashLengths, 2 );
            CGContextSetLineWidth( mrContext, 2.0);
            CGContextStrokeRect ( mrContext, aCGRect );
        }
        else if ( nFlags & SAL_INVERT_50 )
        {
            //CGContextSetAllowsAntialiasing( mrContext, false );
            CGContextSetBlendMode(mrContext, kCGBlendModeDifference);
            CGContextAddRect( mrContext, aCGRect );
            Pattern50Fill();
		}
        else // just invert
        {
            CGContextSetBlendMode(mrContext, kCGBlendModeDifference);
            CGContextSetRGBFillColor ( mrContext,1.0, 1.0, 1.0 , 1.0 );
            CGContextFillRect ( mrContext, aCGRect );
        }
        CGContextRestoreGState( mrContext);
        RefreshRect( aCGRect );
    }
}

// -----------------------------------------------------------------------

void AquaSalGraphics::invert( sal_uInt32 nPoints, const SalPoint* pPtAry, SalInvert nSalFlags )
{
    CGPoint* CGpoints ;
    if ( CheckContext() )
    {
        CGContextSaveGState(mrContext);
        CGpoints = makeCGptArray(nPoints,pPtAry);
        CGContextAddLines ( mrContext, CGpoints, nPoints );
        if ( nSalFlags & SAL_INVERT_TRACKFRAME )
        {
            const CGFloat dashLengths[2]  = { 4.0, 4.0 };     // for drawing dashed line
            CGContextSetBlendMode( mrContext, kCGBlendModeDifference );
            CGContextSetRGBStrokeColor ( mrContext, 1.0, 1.0, 1.0, 1.0 );
            CGContextSetLineDash ( mrContext, 0, dashLengths, 2 );
            CGContextSetLineWidth( mrContext, 2.0);
            CGContextStrokePath ( mrContext );
        }
        else if ( nSalFlags & SAL_INVERT_50 )
        {
            CGContextSetBlendMode(mrContext, kCGBlendModeDifference);
            Pattern50Fill();
        }
        else // just invert                   
        {
            CGContextSetBlendMode( mrContext, kCGBlendModeDifference );
            CGContextSetRGBFillColor( mrContext, 1.0, 1.0, 1.0, 1.0 );
            CGContextFillPath( mrContext );
        }
        const CGRect aRefreshRect = CGContextGetClipBoundingBox(mrContext);
        CGContextRestoreGState( mrContext);
        delete []  CGpoints;
        RefreshRect( aRefreshRect );
    }
}

// -----------------------------------------------------------------------

sal_Bool AquaSalGraphics::drawEPS( long nX, long nY, long nWidth, long nHeight,
	void* pEpsData, sal_uLong nByteCount )
{
	// convert the raw data to an NSImageRef
	NSData* xNSData = [NSData dataWithBytes:(void*)pEpsData length:(int)nByteCount];
	NSImageRep* xEpsImage = [NSEPSImageRep imageRepWithData: xNSData];
	if( !xEpsImage )
		return false;

	// get the target context
	if( !CheckContext() )
		return false;

	// NOTE: flip drawing, else the nsimage would be drawn upside down
	CGContextSaveGState( mrContext );
//	CGContextTranslateCTM( mrContext, 0, +mnHeight );
	CGContextScaleCTM( mrContext, +1, -1 );
	nY = /*mnHeight*/ - (nY + nHeight);

	// prepare the target context
	NSGraphicsContext* pOrigNSCtx = [NSGraphicsContext currentContext];
	[pOrigNSCtx retain];
	
	// create new context
	NSGraphicsContext* pDrawNSCtx = [NSGraphicsContext graphicsContextWithGraphicsPort: mrContext flipped: IsFlipped()];
	// set it, setCurrentContext also releases the previously set one
	[NSGraphicsContext setCurrentContext: pDrawNSCtx];
	
	// draw the EPS
	const NSRect aDstRect = NSMakeRect( nX, nY, nWidth, nHeight);
	const BOOL bOK = [xEpsImage drawInRect: aDstRect];

	// restore the NSGraphicsContext
	[NSGraphicsContext setCurrentContext: pOrigNSCtx];
	[pOrigNSCtx release]; // restore the original retain count

	CGContextRestoreGState( mrContext );
	// mark the destination rectangle as updated
   	RefreshRect( aDstRect );

	return bOK;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
bool AquaSalGraphics::drawAlphaBitmap( const SalTwoRect& rTR,
    const SalBitmap& rSrcBitmap, const SalBitmap& rAlphaBmp )
{
    // An image mask can't have a depth > 8 bits (should be 1 to 8 bits)
    if( rAlphaBmp.GetBitCount() > 8 )
        return false;

    // are these two tests really necessary? (see vcl/unx/source/gdi/salgdi2.cxx)
    // horizontal/vertical mirroring not implemented yet
    if( rTR.mnDestWidth < 0 || rTR.mnDestHeight < 0 )
        return false;

    const AquaSalBitmap& rSrcSalBmp = static_cast<const AquaSalBitmap&>(rSrcBitmap);
    const AquaSalBitmap& rMaskSalBmp = static_cast<const AquaSalBitmap&>(rAlphaBmp);

    CGImageRef xMaskedImage = rSrcSalBmp.CreateWithMask( rMaskSalBmp, rTR.mnSrcX, rTR.mnSrcY, rTR.mnSrcWidth, rTR.mnSrcHeight );
	if( !xMaskedImage )
		return false;

    if ( CheckContext() )
    {
		const CGRect aDstRect = CGRectMake( rTR.mnDestX, rTR.mnDestY, rTR.mnDestWidth, rTR.mnDestHeight);
		CGContextDrawImage( mrContext, aDstRect, xMaskedImage );
		RefreshRect( aDstRect );
    }

    CGImageRelease(xMaskedImage);
    return true;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
bool AquaSalGraphics::drawTransformedBitmap(
	const basegfx::B2DPoint& rNull, const basegfx::B2DPoint& rX, const basegfx::B2DPoint& rY,
	const SalBitmap& rSrcBitmap, const SalBitmap* pAlphaBmp )
{
	if( !CheckContext() )
		return true;

	// get the Quartz image
	CGImageRef xImage = NULL;
	const Size aSize = rSrcBitmap.GetSize();
	const AquaSalBitmap& rSrcSalBmp = static_cast<const AquaSalBitmap&>(rSrcBitmap);
	const AquaSalBitmap* pMaskSalBmp = static_cast<const AquaSalBitmap*>(pAlphaBmp);
	if( !pMaskSalBmp)
		xImage = rSrcSalBmp.CreateCroppedImage( 0, 0, (int)aSize.Width(), (int)aSize.Height() );
	else
		xImage = rSrcSalBmp.CreateWithMask( *pMaskSalBmp, 0, 0, (int)aSize.Width(), (int)aSize.Height() );
	if( !xImage )
		return false;

	// setup the image transformation
	// using the rNull,rX,rY points as destinations for the (0,0),(0,Width),(Height,0) source points
	CGContextSaveGState( mrContext );
	const basegfx::B2DVector aXRel = rX - rNull;
	const basegfx::B2DVector aYRel = rY - rNull;
	const CGAffineTransform aCGMat = CGAffineTransformMake(
		aXRel.getX()/aSize.Width(), aXRel.getY()/aSize.Width(),
		aYRel.getX()/aSize.Height(), aYRel.getY()/aSize.Height(),
		rNull.getX(), rNull.getY());
	CGContextConcatCTM( mrContext, aCGMat );

	// draw the transformed image
	const CGRect aSrcRect = CGRectMake( 0, 0, aSize.Width(), aSize.Height());
	CGContextDrawImage( mrContext, aSrcRect, xImage );
	CGImageRelease( xImage );
	// restore the Quartz graphics state
	CGContextRestoreGState(mrContext);

	// mark the destination as painted
	const CGRect aDstRect = CGRectApplyAffineTransform( aSrcRect, aCGMat );
	RefreshRect( aDstRect );
	return true;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
bool AquaSalGraphics::drawAlphaRect( long nX, long nY, long nWidth,
                                     long nHeight, sal_uInt8 nTransparency )
{
	if( !CheckContext() )
		return true;
    	
	// save the current state
	CGContextSaveGState( mrContext );
	CGContextSetAlpha( mrContext, (100-nTransparency) * (1.0/100) ); 

	CGRect aRect = CGRectMake( nX, nY, nWidth-1, nHeight-1);
	if( IsPenVisible() )
	{
		aRect.origin.x += 0.5;
		aRect.origin.y += 0.5;
	}

	CGContextBeginPath( mrContext );
	CGContextAddRect( mrContext, aRect );
	CGContextDrawPath( mrContext, kCGPathFill );

	// restore state
	CGContextRestoreGState(mrContext);
	RefreshRect( aRect );
	return true;
}

// -----------------------------------------------------------------------

void AquaSalGraphics::SetTextColor( SalColor nSalColor )
{
	maTextColor = RGBAColor( nSalColor );
	if( mpMacTextStyle)
		mpMacTextStyle->SetTextColor( maTextColor );
}

// -----------------------------------------------------------------------

void AquaSalGraphics::GetFontMetric( ImplFontMetricData* pMetric, int /*nFallbackLevel*/ )
{
	mpMacTextStyle->GetFontMetric( mfFakeDPIScale, *pMetric );
}

// -----------------------------------------------------------------------

sal_uLong AquaSalGraphics::GetKernPairs( sal_uLong, ImplKernPairData* )
{
	return 0;
}

// -----------------------------------------------------------------------

static bool AddTempFontDir( const char* pDir )
{
    FSRef aPathFSRef;
    Boolean bIsDirectory = true;
    OSStatus eStatus = FSPathMakeRef( reinterpret_cast<const UInt8*>(pDir), &aPathFSRef, &bIsDirectory );
    DBG_ASSERTWARNING( (eStatus==noErr) && bIsDirectory, "vcl AddTempFontDir() with invalid directory name!" );
    if( eStatus != noErr )
        return false;

    // TODO: deactivate ATSFontContainerRef when closing app
    ATSFontContainerRef aATSFontContainer;

    const ATSFontContext eContext = kATSFontContextLocal; // TODO: *Global???
#if defined(MAC_OS_X_VERSION_10_5) && (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5)
    eStatus = ::ATSFontActivateFromFileReference( &aPathFSRef,
        eContext, kATSFontFormatUnspecified, NULL, kATSOptionFlagsDefault,
        &aATSFontContainer );
#else
    FSSpec aPathFSSpec;
    eStatus = ::FSGetCatalogInfo( &aPathFSRef, kFSCatInfoNone,
        NULL, NULL, &aPathFSSpec, NULL );
    if( eStatus != noErr )
        return false;

    eStatus = ::ATSFontActivateFromFileSpecification( &aPathFSSpec,
        eContext, kATSFontFormatUnspecified, NULL, kATSOptionFlagsDefault,
        &aATSFontContainer );
#endif
    if( eStatus != noErr )
        return false;

    return true;
}

static bool AddLocalTempFontDirs( void )
{
    static bool bFirst = true;
    if( !bFirst )
        return false;
    bFirst = false;

    // add private font files found in office base dir
    
    // rtl::OUString aBrandStr( RTL_CONSTASCII_USTRINGPARAM( "$OOO_BASE_DIR" ) );
    // rtl_bootstrap_expandMacros( &aBrandStr.pData );
    // rtl::OUString aBrandSysPath;
    // OSL_VERIFY( osl_getSystemPathFromFileURL( aBrandStr.pData, &aBrandSysPath.pData ) == osl_File_E_None );
    
    // rtl::OStringBuffer aBrandFontDir( aBrandSysPath.getLength()*2 );
    // aBrandFontDir.append( rtl::OUStringToOString( aBrandSysPath, RTL_TEXTENCODING_UTF8 ) );
    // aBrandFontDir.append( "/share/fonts/truetype/" );
    // bool bBrandSuccess = AddTempFontDir( aBrandFontDir.getStr() );

    rtl::OUString aBaseStr( RTL_CONSTASCII_USTRINGPARAM( "$OOO_BASE_DIR" ) );
    rtl_bootstrap_expandMacros( &aBaseStr.pData );
    rtl::OUString aBaseSysPath;
    OSL_VERIFY( osl_getSystemPathFromFileURL( aBaseStr.pData, &aBaseSysPath.pData ) == osl_File_E_None );
    
    rtl::OStringBuffer aBaseFontDir( aBaseSysPath.getLength()*2 );
    aBaseFontDir.append( rtl::OUStringToOString( aBaseSysPath, RTL_TEXTENCODING_UTF8 ) );
    aBaseFontDir.append( "/share/fonts/truetype/" );
    bool bBaseSuccess = AddTempFontDir( aBaseFontDir.getStr() );

//    return bBrandSuccess && bBaseSuccess;
    return bBaseSuccess;
}

void AquaSalGraphics::GetDevFontList( ImplDevFontList* pFontList )
{
	DBG_ASSERT( pFontList, "AquaSalGraphics::GetDevFontList(NULL) !");

	AddLocalTempFontDirs();
 
	// The idea is to cache the list of system fonts once it has been generated. 
	// SalData seems to be a good place for this caching. However we have to 
	// carefully make the access to the font list thread-safe. If we register 
	// a font-change event handler to update the font list in case fonts have 
	// changed on the system we have to lock access to the list. The right
	// way to do that is the solar mutex since GetDevFontList is protected
	// through it as should be all event handlers

	SalData* pSalData = GetSalData();
#ifdef USE_ATSU
	SystemFontList* GetAtsFontList(void);      // forward declaration
	if( !pSalData->mpFontList )
		pSalData->mpFontList = GetAtsFontList();
#else
	SystemFontList* GetCoretextFontList(void); // forward declaration
	if( !pSalData->mpFontList )
		pSalData->mpFontList = GetCoretextFontList();
#endif // DISABLE_ATSUI

	// Copy all ImplFontData objects contained in the SystemFontList
	pSalData->mpFontList->AnnounceFonts( *pFontList );
}

// -----------------------------------------------------------------------

bool AquaSalGraphics::AddTempDevFont( ImplDevFontList*,
	const String& rFontFileURL, const String& /*rFontName*/ )
{
	::rtl::OUString aUSytemPath;
    OSL_VERIFY( !osl::FileBase::getSystemPathFromFileURL( rFontFileURL, aUSytemPath ) );

	FSRef aNewRef;
	Boolean bIsDirectory = true;
	::rtl::OString aCFileName = rtl::OUStringToOString( aUSytemPath, RTL_TEXTENCODING_UTF8 );
	OSStatus eStatus = FSPathMakeRef( (UInt8*)aCFileName.getStr(), &aNewRef, &bIsDirectory );
    DBG_ASSERT( (eStatus==noErr) && !bIsDirectory, "vcl AddTempDevFont() with invalid fontfile name!" );
	if( eStatus != noErr )
		return false;

	ATSFontContainerRef oContainer;

	const ATSFontContext eContext = kATSFontContextLocal; // TODO: *Global???
#if defined(MAC_OS_X_VERSION_10_5) && (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5)
	eStatus = ::ATSFontActivateFromFileReference( &aNewRef,
		eContext, kATSFontFormatUnspecified, NULL, kATSOptionFlagsDefault,
		&oContainer );
#else
	FSSpec aFontFSSpec;
	eStatus = ::FSGetCatalogInfo( &aNewRef, kFSCatInfoNone,
		NULL, NULL,	&aFontFSSpec, NULL );
	if( eStatus != noErr )
		return false;

	eStatus = ::ATSFontActivateFromFileSpecification( &aFontFSSpec,
		eContext, kATSFontFormatUnspecified, NULL, kATSOptionFlagsDefault,
		&oContainer );
#endif
	if( eStatus != noErr )
		return false;

	// TODO: ATSFontDeactivate( oContainer ) when fonts are no longer needed
	// TODO: register new ImplMacFontdata in pFontList
    return true;
}

// -----------------------------------------------------------------------

long AquaSalGraphics::GetGraphicsWidth() const
{
    long w = 0;
    if( mrContext && (mbWindow || mbVirDev) )
    {
        w = mnWidth;
    }
    
    if( w == 0 )
    {
        if( mbWindow && mpFrame )
            w = mpFrame->maGeometry.nWidth;
    }
    
    return w;
}

// -----------------------------------------------------------------------

bool AquaSalGraphics::GetGlyphBoundRect( sal_GlyphId aGlyphId, Rectangle& rRect )
{
	const bool bRC = mpMacTextStyle->GetGlyphBoundRect( aGlyphId, rRect );
	return bRC;
}

// -----------------------------------------------------------------------

bool AquaSalGraphics::GetGlyphOutline( sal_GlyphId aGlyphId, basegfx::B2DPolyPolygon& rPolyPoly )
{
	const bool bRC = mpMacTextStyle->GetGlyphOutline( aGlyphId, rPolyPoly );
	return bRC;
}

// -----------------------------------------------------------------------

void AquaSalGraphics::GetDevFontSubstList( OutputDevice* )
{
	// nothing to do since there are no device-specific fonts on Aqua
}

// -----------------------------------------------------------------------

void AquaSalGraphics::DrawServerFontLayout( const ServerFontLayout& )
{}

// -----------------------------------------------------------------------

sal_uInt16 AquaSalGraphics::SetFont( ImplFontSelectData* pReqFont, int /*nFallbackLevel*/ )
{
	// release the text style
	delete mpMacTextStyle;
	mpMacTextStyle = NULL;

	// handle NULL request meaning: release-font-resources request
	if( !pReqFont )
	{
		mpMacFontData = NULL;
		return 0;
	}

	// update the text style
	mpMacFontData = static_cast<const ImplMacFontData*>( pReqFont->mpFontData );
	mpMacTextStyle = mpMacFontData->CreateMacTextStyle( *pReqFont );
	mpMacTextStyle->SetTextColor( maTextColor );

#if (OSL_DEBUG_LEVEL > 3)
	fprintf( stderr, "SetFont to (\"%s\", \"%s\", fontid=%d) for (\"%s\" \"%s\" weight=%d, slant=%d size=%dx%d orientation=%d)\n",
		::rtl::OUStringToOString( mpMacFontData->GetFamilyName(), RTL_TEXTENCODING_UTF8 ).getStr(),
		::rtl::OUStringToOString( mpMacFontData->GetStyleName(), RTL_TEXTENCODING_UTF8 ).getStr(),
		(int)pMacFont->GetFontId(),
		::rtl::OUStringToOString( mpMacFontData->GetFamilyName(), RTL_TEXTENCODING_UTF8 ).getStr(),
		::rtl::OUStringToOString( mpMacFontData->GetStyleName(), RTL_TEXTENCODING_UTF8 ).getStr(),
		pReqFont->GetWeight(),
		pReqFont->GetSlant(),
		pReqFont->mnHeight,
		pReqFont->mnWidth,
		pReqFont->mnOrientation);
#endif

    return 0;
}

// -----------------------------------------------------------------------

SalLayout* AquaSalGraphics::GetTextLayout( ImplLayoutArgs& /*rArgs*/, int /*nFallbackLevel*/ )
{
	SalLayout* pSalLayout = mpMacTextStyle->GetTextLayout();
	return pSalLayout;
}

// -----------------------------------------------------------------------

const ImplFontCharMap* AquaSalGraphics::GetImplFontCharMap() const
{
	if( !mpMacFontData )
		return ImplFontCharMap::GetDefaultMap();

	return mpMacFontData->GetImplFontCharMap();
}
	
// -----------------------------------------------------------------------

// fake a SFNT font directory entry for a font table
// see http://developer.apple.com/textfonts/TTRefMan/RM06/Chap6.html#Directory
static void FakeDirEntry( const char aTag[5], ByteCount nOfs, ByteCount nLen,
	const unsigned char* /*pData*/, unsigned char*& rpDest )
{
	// write entry tag
	rpDest[ 0] = aTag[0];
	rpDest[ 1] = aTag[1];
	rpDest[ 2] = aTag[2];
	rpDest[ 3] = aTag[3];
	// TODO: get entry checksum and write it
	//       not too important since the subsetter doesn't care currently
	//       for( pData+nOfs ... pData+nOfs+nLen )
	// write entry offset
	rpDest[ 8] = (char)(nOfs >> 24);
	rpDest[ 9] = (char)(nOfs >> 16);
	rpDest[10] = (char)(nOfs >>  8);
	rpDest[11] = (char)(nOfs >>  0);
	// write entry length
	rpDest[12] = (char)(nLen >> 24);
	rpDest[13] = (char)(nLen >> 16);
	rpDest[14] = (char)(nLen >>  8);
	rpDest[15] = (char)(nLen >>  0);
	// advance to next entry
	rpDest += 16;
}

// fake a TTF or CFF font as directly accessing font file is not possible
// when only the fontid is known. This approach also handles *.dfont fonts.
static bool GetRawFontData( const ImplFontData* pFontData,
	ByteVector& rBuffer, bool* pJustCFF )
{
	const ImplMacFontData* pMacFont = static_cast<const ImplMacFontData*>(pFontData);

	// short circuit for CFF-only fonts
	const int nCffSize = pMacFont->GetFontTable( "CFF ", NULL);
	if( pJustCFF != NULL )
	{
		*pJustCFF = (nCffSize > 0);
		if( *pJustCFF)
		{
			rBuffer.resize( nCffSize);
			const int nCffRead = pMacFont->GetFontTable( "CFF ", &rBuffer[0]);
			if( nCffRead != nCffSize)
				return false;
			return true;
		}
	}

	// get font table availability and size in bytes
	const int nHeadSize = pMacFont->GetFontTable( "head", NULL);
	if( nHeadSize <= 0)
		return false;
	const int nMaxpSize = pMacFont->GetFontTable( "maxp", NULL);
	if( nMaxpSize <= 0)
		return false;
	const int nCmapSize = pMacFont->GetFontTable( "cmap", NULL);
	if( nCmapSize <= 0)
		return false;
	const int nNameSize = pMacFont->GetFontTable( "name", NULL);
	if( nNameSize <= 0)
		return false;
	const int nHheaSize = pMacFont->GetFontTable( "hhea", NULL);
	if( nHheaSize <= 0)
		return false;
	const int nHmtxSize = pMacFont->GetFontTable( "hmtx", NULL);
	if( nHmtxSize <= 0)
		return false;

	// get the ttf-glyf outline tables
	int nLocaSize = 0;
	int nGlyfSize = 0;
	if( nCffSize <= 0)
	{
		nLocaSize = pMacFont->GetFontTable( "loca", NULL);
		if( nLocaSize <= 0)
			return false;
		nGlyfSize = pMacFont->GetFontTable( "glyf", NULL);
		if( nGlyfSize <= 0)
			return false;
	}

	int nPrepSize = 0, nCvtSize = 0, nFpgmSize = 0;
	if( nGlyfSize) // TODO: reduce PDF size by making hint subsetting optional
	{
		nPrepSize = pMacFont->GetFontTable( "prep", NULL);
		nCvtSize  = pMacFont->GetFontTable( "cvt ", NULL);
		nFpgmSize = pMacFont->GetFontTable( "fpgm", NULL);
	}
	
	// prepare a byte buffer for a fake font
	int nTableCount = 7;
	nTableCount += (nPrepSize>0) + (nCvtSize>0) + (nFpgmSize>0) + (nGlyfSize>0);
	const ByteCount nFdirSize = 12 + 16*nTableCount;
	ByteCount nTotalSize = nFdirSize;
	nTotalSize += nHeadSize + nMaxpSize + nNameSize + nCmapSize;
	if( nGlyfSize )
		nTotalSize += nLocaSize + nGlyfSize;
	else
		nTotalSize += nCffSize;
	nTotalSize += nHheaSize + nHmtxSize;
	nTotalSize += nPrepSize + nCvtSize + nFpgmSize;
	rBuffer.resize( nTotalSize );

	// fake a SFNT font directory header
	if( nTableCount < 16 )
	{
		int nLog2 = 0;
		while( (nTableCount >> nLog2) > 1 ) ++nLog2;
		rBuffer[ 1] = 1;						// Win-TTF style scaler
		rBuffer[ 5] = nTableCount;				// table count
		rBuffer[ 7] = nLog2*16;					// searchRange
		rBuffer[ 9] = nLog2;					// entrySelector
		rBuffer[11] = (nTableCount-nLog2)*16;	// rangeShift
	}

	// get font table raw data and update the fake directory entries
	ByteCount nOfs = nFdirSize;
	unsigned char* pFakeEntry = &rBuffer[12];
	if( nCmapSize != pMacFont->GetFontTable( "cmap", &rBuffer[nOfs]))
		return false;
	FakeDirEntry( "cmap", nOfs, nCmapSize, &rBuffer[0], pFakeEntry );
	nOfs += nCmapSize;
	if( nCvtSize ) {
		if( nCvtSize != pMacFont->GetFontTable( "cvt ", &rBuffer[nOfs]))
			return false;
		FakeDirEntry( "cvt ", nOfs, nCvtSize, &rBuffer[0], pFakeEntry );
		nOfs += nCvtSize;
	}
	if( nFpgmSize ) {
		if( nFpgmSize != pMacFont->GetFontTable( "fpgm", &rBuffer[nOfs]))
			return false;
		FakeDirEntry( "fpgm", nOfs, nFpgmSize, &rBuffer[0], pFakeEntry );
		nOfs += nFpgmSize;
	}
	if( nCffSize ) {
		if( nCffSize != pMacFont->GetFontTable( "CFF ", &rBuffer[nOfs]))
			return false;
		FakeDirEntry( "CFF ", nOfs, nCffSize, &rBuffer[0], pFakeEntry );
		nOfs += nGlyfSize;
	} else {
		if( nGlyfSize != pMacFont->GetFontTable( "glyf", &rBuffer[nOfs]))
			return false;
		FakeDirEntry( "glyf", nOfs, nGlyfSize, &rBuffer[0], pFakeEntry );
		nOfs += nGlyfSize;
		if( nLocaSize != pMacFont->GetFontTable( "loca", &rBuffer[nOfs]))
			return false;
		FakeDirEntry( "loca", nOfs, nLocaSize, &rBuffer[0], pFakeEntry );
		nOfs += nLocaSize;
	}
	if( nHeadSize != pMacFont->GetFontTable( "head", &rBuffer[nOfs]))
		return false;
	FakeDirEntry( "head", nOfs, nHeadSize, &rBuffer[0], pFakeEntry );
	nOfs += nHeadSize;
	if( nHheaSize != pMacFont->GetFontTable( "hhea", &rBuffer[nOfs]))
		return false;
	FakeDirEntry( "hhea", nOfs, nHheaSize, &rBuffer[0], pFakeEntry );
	nOfs += nHheaSize;
	if( nHmtxSize != pMacFont->GetFontTable( "hmtx", &rBuffer[nOfs]))
		return false;
	FakeDirEntry( "hmtx", nOfs, nHmtxSize, &rBuffer[0], pFakeEntry );
	nOfs += nHmtxSize;
	if( nMaxpSize != pMacFont->GetFontTable( "maxp", &rBuffer[nOfs]))
		return false;
	FakeDirEntry( "maxp", nOfs, nMaxpSize, &rBuffer[0], pFakeEntry );
	nOfs += nMaxpSize;
	if( nNameSize != pMacFont->GetFontTable( "name", &rBuffer[nOfs]))
		return false;
	FakeDirEntry( "name", nOfs, nNameSize, &rBuffer[0], pFakeEntry );
	nOfs += nNameSize;
	if( nPrepSize ) {
		if( nPrepSize != pMacFont->GetFontTable( "prep", &rBuffer[nOfs]))
			return false;
		FakeDirEntry( "prep", nOfs, nPrepSize, &rBuffer[0], pFakeEntry );
		nOfs += nPrepSize;
	}

	DBG_ASSERT( (nOfs==nTotalSize), "AquaSalGraphics::CreateFontSubset (nOfs!=nTotalSize)");

	return true;
}

sal_Bool AquaSalGraphics::CreateFontSubset( const rtl::OUString& rToFile,
	const ImplFontData* pFontData, sal_GlyphId* pGlyphIds, sal_uInt8* pEncoding,
	sal_Int32* pGlyphWidths, int nGlyphCount, FontSubsetInfo& rInfo )
{
	// TODO: move more of the functionality here into the generic subsetter code

	// prepare the requested file name for writing the font-subset file
    rtl::OUString aSysPath;
    if( osl_File_E_None != osl_getSystemPathFromFileURL( rToFile.pData, &aSysPath.pData ) )
        return sal_False;
    const rtl_TextEncoding aThreadEncoding = osl_getThreadTextEncoding();
    const ByteString aToFile( rtl::OUStringToOString( aSysPath, aThreadEncoding ) );

	// get the raw-bytes from the font to be subset
	ByteVector aBuffer;
	bool bCffOnly = false;
	if( !GetRawFontData( pFontData, aBuffer, &bCffOnly ) )
		return sal_False;

	// handle CFF-subsetting
	if( bCffOnly )
	{
		// provide the raw-CFF data to the subsetter
		const ByteCount nCffLen = aBuffer.size();
		rInfo.LoadFont( FontSubsetInfo::CFF_FONT, &aBuffer[0], nCffLen );

		// NOTE: assuming that all glyphids requested on Aqua are fully translated

		// make the subsetter provide the requested subset
		FILE* pOutFile = fopen( aToFile.GetBuffer(), "wb" );
		bool bRC = rInfo.CreateFontSubset( FontSubsetInfo::TYPE1_PFB, pOutFile, NULL,
			pGlyphIds, pEncoding, nGlyphCount, pGlyphWidths );
		fclose( pOutFile );
		return bRC;
	}

	// TODO: modernize psprint's horrible fontsubset C-API
	// this probably only makes sense after the switch to another SCM
	// that can preserve change history after file renames

	// prepare data for psprint's font subsetter
	TrueTypeFont* pSftFont = NULL;
	int nRC = ::OpenTTFontBuffer( (void*)&aBuffer[0], aBuffer.size(), 0, &pSftFont);
	if( nRC != SF_OK )
		return sal_False;

	// get details about the subsetted font
	TTGlobalFontInfo aTTInfo;
	::GetTTGlobalFontInfo( pSftFont, &aTTInfo );
	rInfo.m_nFontType   = FontSubsetInfo::SFNT_TTF;
	rInfo.m_aPSName     = String( aTTInfo.psname, RTL_TEXTENCODING_UTF8 );
	rInfo.m_aFontBBox   = Rectangle( Point( aTTInfo.xMin, aTTInfo.yMin ),
                                    Point( aTTInfo.xMax, aTTInfo.yMax ) );
	rInfo.m_nCapHeight  = aTTInfo.yMax; // Well ...
	rInfo.m_nAscent     = aTTInfo.winAscent;
	rInfo.m_nDescent    = aTTInfo.winDescent;
	// mac fonts usually do not have an OS2-table
	// => get valid ascent/descent values from other tables
	if( !rInfo.m_nAscent )
		rInfo.m_nAscent	= +aTTInfo.typoAscender;
	if( !rInfo.m_nAscent )
		rInfo.m_nAscent	= +aTTInfo.ascender;
	if( !rInfo.m_nDescent )
		rInfo.m_nDescent = +aTTInfo.typoDescender;
	if( !rInfo.m_nDescent )
		rInfo.m_nDescent = -aTTInfo.descender;

    // subset glyphs and get their properties
    // take care that subset fonts require the NotDef glyph in pos 0
    int nOrigCount = nGlyphCount;
    sal_uInt16 aShortIDs[ 256 ];
    sal_uInt8 aTempEncs[ 256 ];

    int nNotDef = -1;
    for( int i = 0; i < nGlyphCount; ++i )
    {
        aTempEncs[i] = pEncoding[i];
        sal_GlyphId aGlyphId( pGlyphIds[i] & GF_IDXMASK);
        if( pGlyphIds[i] & GF_ISCHAR )
        {
            bool bVertical = (pGlyphIds[i] & GF_ROTMASK) != 0;
            aGlyphId = ::MapChar( pSftFont, static_cast<sal_uInt16>(aGlyphId), bVertical );
            if( aGlyphId == 0 && pFontData->IsSymbolFont() )
            {
                // #i12824# emulate symbol aliasing U+FXXX <-> U+0XXX
                aGlyphId = pGlyphIds[i] & GF_IDXMASK;
                aGlyphId = (aGlyphId & 0xF000) ? (aGlyphId & 0x00FF) : (aGlyphId | 0xF000 );
                aGlyphId = ::MapChar( pSftFont, static_cast<sal_uInt16>(aGlyphId), bVertical );
            }
        }
        aShortIDs[i] = static_cast<sal_uInt16>( aGlyphId );
        if( !aGlyphId )
            if( nNotDef < 0 )
                nNotDef = i; // first NotDef glyph found
    }

    if( nNotDef != 0 )
    {
        // add fake NotDef glyph if needed
        if( nNotDef < 0 )
            nNotDef = nGlyphCount++;

        // NotDef glyph must be in pos 0 => swap glyphids
        aShortIDs[ nNotDef ] = aShortIDs[0];
        aTempEncs[ nNotDef ] = aTempEncs[0];
        aShortIDs[0] = 0;
        aTempEncs[0] = 0;
    }
    DBG_ASSERT( nGlyphCount < 257, "too many glyphs for subsetting" );

	// TODO: where to get bVertical?
	const bool bVertical = false;

    // fill the pGlyphWidths array
	// while making sure that the NotDef glyph is at index==0
    TTSimpleGlyphMetrics* pGlyphMetrics =
        ::GetTTSimpleGlyphMetrics( pSftFont, aShortIDs, nGlyphCount, bVertical );
    if( !pGlyphMetrics )
        return sal_False;
    sal_uInt16 nNotDefAdv   	= pGlyphMetrics[0].adv;
    pGlyphMetrics[0].adv    	= pGlyphMetrics[nNotDef].adv;
    pGlyphMetrics[nNotDef].adv	= nNotDefAdv;
    for( int i = 0; i < nOrigCount; ++i )
        pGlyphWidths[i] = pGlyphMetrics[i].adv;
    free( pGlyphMetrics );

    // write subset into destination file
    nRC = ::CreateTTFromTTGlyphs( pSftFont, aToFile.GetBuffer(), aShortIDs,
            aTempEncs, nGlyphCount, 0, NULL, 0 );
	::CloseTTFont(pSftFont);
    return (nRC == SF_OK);
}

// -----------------------------------------------------------------------

void AquaSalGraphics::GetGlyphWidths( const ImplFontData* pFontData, bool bVertical,
	Int32Vector& rGlyphWidths, Ucs2UIntMap& rUnicodeEnc )
{
    rGlyphWidths.clear();
    rUnicodeEnc.clear();

	if( pFontData->IsSubsettable() )
	{
		ByteVector aBuffer;
		if( !GetRawFontData( pFontData, aBuffer, NULL ) )
			return;

		// TODO: modernize psprint's horrible fontsubset C-API
		// this probably only makes sense after the switch to another SCM
		// that can preserve change history after file renames

		// use the font subsetter to get the widths
		TrueTypeFont* pSftFont = NULL;
		int nRC = ::OpenTTFontBuffer( (void*)&aBuffer[0], aBuffer.size(), 0, &pSftFont);
		if( nRC != SF_OK )
			return;

        const int nGlyphCount = ::GetTTGlyphCount( pSftFont );
        if( nGlyphCount > 0 )
        {
			// get glyph metrics
            rGlyphWidths.resize(nGlyphCount);
            std::vector<sal_uInt16> aGlyphIds(nGlyphCount);
            for( int i = 0; i < nGlyphCount; i++ )
                aGlyphIds[i] = static_cast<sal_uInt16>(i);
            const TTSimpleGlyphMetrics* pGlyphMetrics = ::GetTTSimpleGlyphMetrics(
				pSftFont, &aGlyphIds[0], nGlyphCount, bVertical );
            if( pGlyphMetrics )
            {
                for( int i = 0; i < nGlyphCount; ++i )
                    rGlyphWidths[i] = pGlyphMetrics[i].adv;
                free( (void*)pGlyphMetrics );
            }

            const ImplFontCharMap* pMap = mpMacFontData->GetImplFontCharMap();
            DBG_ASSERT( pMap && pMap->GetCharCount(), "no charmap" );
            pMap->AddReference(); // TODO: add and use RAII object instead

			// get unicode<->glyph encoding
			// TODO? avoid sft mapping by using the pMap itself
			int nCharCount = pMap->GetCharCount();
			sal_uInt32 nChar = pMap->GetFirstChar();
			for(; --nCharCount >= 0; nChar = pMap->GetNextChar( nChar ) )
            {
				if( nChar > 0xFFFF ) // TODO: allow UTF-32 chars
					break;
				sal_Ucs nUcsChar = static_cast<sal_Ucs>(nChar);
                sal_uInt32 nGlyph = ::MapChar( pSftFont, nUcsChar, bVertical );
                if( nGlyph > 0 )
                    rUnicodeEnc[ nUcsChar ] = nGlyph;
            }

            pMap->DeReference(); // TODO: add and use RAII object instead
        }

		::CloseTTFont( pSftFont );
    }
    else if( pFontData->IsEmbeddable() )
    {
        // get individual character widths
#if 0 // FIXME
        rWidths.reserve( 224 );
        for( sal_Unicode i = 32; i < 256; ++i )
        {
            int nCharWidth = 0;
            if( ::GetCharWidth32W( mhDC, i, i, &nCharWidth ) )
            {
                rUnicodeEnc[ i ] = rWidths.size();
                rWidths.push_back( nCharWidth );
            }
        }
#else
		DBG_ERROR("not implemented for non-subsettable fonts!\n");
#endif
    }    
}

// -----------------------------------------------------------------------

const Ucs2SIntMap* AquaSalGraphics::GetFontEncodingVector(
	const ImplFontData*, const Ucs2OStrMap** /*ppNonEncoded*/ )
{
	return NULL;
}

// -----------------------------------------------------------------------

const void*	AquaSalGraphics::GetEmbedFontData( const ImplFontData*,
                              const sal_Ucs* /*pUnicodes*/,
                              sal_Int32* /*pWidths*/,
                              FontSubsetInfo&,
                              long* /*pDataLen*/ )
{
	return NULL;
}

// -----------------------------------------------------------------------

void AquaSalGraphics::FreeEmbedFontData( const void* pData, long /*nDataLen*/ )
{
	// TODO: implementing this only makes sense when the implementation of
	//		AquaSalGraphics::GetEmbedFontData() returns non-NULL
	(void)pData;
	DBG_ASSERT( (pData!=NULL), "AquaSalGraphics::FreeEmbedFontData() is not implemented\n");
}

// -----------------------------------------------------------------------

SystemFontData AquaSalGraphics::GetSysFontData( int /* nFallbacklevel */ ) const
{
    SystemFontData aSysFontData;
    aSysFontData.nSize = sizeof( SystemFontData );

#ifdef USE_ATSU
    // NOTE: Native ATSU font fallbacks are used, not the VCL fallbacks.
    ATSUFontID fontId; 	 
    OSStatus err;
    err = ATSUGetAttribute( maATSUStyle, kATSUFontTag, sizeof(fontId), &fontId, 0 );
    if (err) fontId = 0;
    aSysFontData.aATSUFontID = (void *) fontId;

    Boolean bFbold;
    err = ATSUGetAttribute( maATSUStyle, kATSUQDBoldfaceTag, sizeof(bFbold), &bFbold, 0 );
    if (err) bFbold = FALSE;
    aSysFontData.bFakeBold = (bool) bFbold;
    
    Boolean bFItalic;    
    err = ATSUGetAttribute( maATSUStyle, kATSUQDItalicTag, sizeof(bFItalic), &bFItalic, 0 );
    if (err) bFItalic = FALSE;
    aSysFontData.bFakeItalic = (bool) bFItalic;

    ATSUVerticalCharacterType aVerticalCharacterType;
    err = ATSUGetAttribute( maATSUStyle, kATSUVerticalCharacterTag, sizeof(aVerticalCharacterType), &aVerticalCharacterType, 0 );
    if (!err && aVerticalCharacterType == kATSUStronglyVertical) {
        aSysFontData.bVerticalCharacterType = true;
    } else {
        aSysFontData.bVerticalCharacterType = false;
    }
#endif // USE_ATSU

    aSysFontData.bAntialias = !mbNonAntialiasedText;

    return aSysFontData;
}

// -----------------------------------------------------------------------

SystemGraphicsData AquaSalGraphics::GetGraphicsData() const
{
    SystemGraphicsData aRes;
    aRes.nSize = sizeof(aRes);
    aRes.rCGContext = mrContext;
    return aRes;
}

// -----------------------------------------------------------------------

void AquaSalGraphics::SetXORMode( bool bSet, bool bInvertOnly )
{
	// return early if XOR mode remains unchanged
    if( mbPrinter )
    	return;
    
    if( ! bSet && mnXorMode == 2 )
    {
        CGContextSetBlendMode( mrContext, kCGBlendModeNormal );
        mnXorMode = 0;
        return;
    }
    else if( bSet && bInvertOnly && mnXorMode == 0)
    {
        CGContextSetBlendMode( mrContext, kCGBlendModeDifference );
        mnXorMode = 2;
        return;
    }

	if( (mpXorEmulation == NULL) && !bSet )
		return;
	if( (mpXorEmulation != NULL) && (bSet == mpXorEmulation->IsEnabled()) )
		return;
	if( !CheckContext() )
	 	return;

	// prepare XOR emulation
	if( !mpXorEmulation )
	{
		mpXorEmulation = new XorEmulation();
		mpXorEmulation->SetTarget( mnWidth, mnHeight, mnBitmapDepth, mrContext, mxLayer );
	}

	// change the XOR mode
	if( bSet )
	{
		mpXorEmulation->Enable();
		mrContext = mpXorEmulation->GetMaskContext();
        mnXorMode = 1;
	}
	else
	{
		mpXorEmulation->UpdateTarget();
		mpXorEmulation->Disable();
		mrContext = mpXorEmulation->GetTargetContext();
        mnXorMode = 0;
	}
}

// -----------------------------------------------------------------------

// apply the XOR mask to the target context if active and dirty
void AquaSalGraphics::ApplyXorContext()
{
	if( !mpXorEmulation )
		return;
	if( mpXorEmulation->UpdateTarget() )
		RefreshRect( 0, 0, mnWidth, mnHeight ); // TODO: refresh minimal changerect
}

// ======================================================================

XorEmulation::XorEmulation()
:	mxTargetLayer( NULL )
,	mxTargetContext( NULL )
,	mxMaskContext( NULL )
,	mxTempContext( NULL )
,	mpMaskBuffer( NULL )
,	mpTempBuffer( NULL )
,	mnBufferLongs( 0 )
,	mbIsEnabled( false )
{}

// ----------------------------------------------------------------------

XorEmulation::~XorEmulation()
{
	Disable();
	SetTarget( 0, 0, 0, NULL, NULL );
}

// -----------------------------------------------------------------------

void XorEmulation::SetTarget( int nWidth, int nHeight, int nTargetDepth,
	CGContextRef xTargetContext, CGLayerRef xTargetLayer )
{
	// prepare to replace old mask+temp context
	if( mxMaskContext )
	{
		// cleanup the mask context
		CGContextRelease( mxMaskContext );
		delete[] mpMaskBuffer;
		mxMaskContext = NULL;
		mpMaskBuffer = NULL;

		// cleanup the temp context if needed
		if( mxTempContext )
		{
			CGContextRelease( mxTempContext );
			delete[] mpTempBuffer;
			mxTempContext = NULL;
			mpTempBuffer = NULL;
		}
	}

	// return early if there is nothing more to do
	if( !xTargetContext )
		return;

	// retarget drawing operations to the XOR mask
	mxTargetLayer = xTargetLayer;
	mxTargetContext = xTargetContext;

	// prepare creation of matching CGBitmaps
	CGColorSpaceRef aCGColorSpace = GetSalData()->mxRGBSpace;
	CGBitmapInfo aCGBmpInfo = kCGImageAlphaNoneSkipFirst;
	int nBitDepth = nTargetDepth;
	if( !nBitDepth )
		nBitDepth = 32;
	int nBytesPerRow = (nBitDepth == 16) ? 2 : 4;
	const size_t nBitsPerComponent = (nBitDepth == 16) ? 5 : 8;
	if( nBitDepth <= 8 )
	{
		aCGColorSpace = GetSalData()->mxGraySpace;
		aCGBmpInfo = kCGImageAlphaNone;
		nBytesPerRow = 1;
	}
	nBytesPerRow *= nWidth;
	mnBufferLongs = (nHeight * nBytesPerRow + sizeof(sal_uLong)-1) / sizeof(sal_uLong);

	// create a XorMask context
	mpMaskBuffer = new sal_uLong[ mnBufferLongs ];
	mxMaskContext = ::CGBitmapContextCreate( mpMaskBuffer,
		nWidth, nHeight, nBitsPerComponent, nBytesPerRow,
		aCGColorSpace, aCGBmpInfo );
	// reset the XOR mask to black
	memset( mpMaskBuffer, 0, mnBufferLongs * sizeof(sal_uLong) );

	// a bitmap context will be needed for manual XORing
	// create one unless the target context is a bitmap context
	if( nTargetDepth )
		mpTempBuffer = (sal_uLong*)CGBitmapContextGetData( mxTargetContext );
	if( !mpTempBuffer )
	{
		// create a bitmap context matching to the target context
		mpTempBuffer = new sal_uLong[ mnBufferLongs ];
		mxTempContext = ::CGBitmapContextCreate( mpTempBuffer,
			nWidth, nHeight, nBitsPerComponent, nBytesPerRow,
			aCGColorSpace, aCGBmpInfo );
	}

	// initialize XOR mask context for drawing
	CGContextSetFillColorSpace( mxMaskContext, aCGColorSpace );
	CGContextSetStrokeColorSpace( mxMaskContext, aCGColorSpace );
	CGContextSetShouldAntialias( mxMaskContext, false );

	// improve the XorMask's XOR emulation a little
	// NOTE: currently only enabled for monochrome contexts
	if( aCGColorSpace == GetSalData()->mxGraySpace )
		CGContextSetBlendMode( mxMaskContext, kCGBlendModeDifference );

	// initialize the transformation matrix to the drawing target
	const CGAffineTransform aCTM = CGContextGetCTM( xTargetContext );
	CGContextConcatCTM( mxMaskContext, aCTM );
	if( mxTempContext )
		CGContextConcatCTM( mxTempContext, aCTM );

	// initialize the default XorMask graphics state
	CGContextSaveGState( mxMaskContext );
}

// ----------------------------------------------------------------------

bool XorEmulation::UpdateTarget()
{
	if( !IsEnabled() )
		return false;

	// update the temp bitmap buffer if needed
	if( mxTempContext )
		CGContextDrawLayerAtPoint( mxTempContext, CGPointZero, mxTargetLayer );

	// do a manual XOR with the XorMask
	// this approach suffices for simple color manipulations
	// and also the complex-clipping-XOR-trick used in metafiles
	const sal_uLong* pSrc = mpMaskBuffer;
	sal_uLong* pDst = mpTempBuffer;
	for( int i = mnBufferLongs; --i >= 0;)
		*(pDst++) ^= *(pSrc++);

	// write back the XOR results to the target context
	if( mxTempContext )
	{
		CGImageRef xXorImage = CGBitmapContextCreateImage( mxTempContext );
		const int nWidth  = (int)CGImageGetWidth( xXorImage );
		const int nHeight = (int)CGImageGetHeight( xXorImage );
		// TODO: update minimal changerect
		const CGRect aFullRect = CGRectMake( 0, 0, nWidth, nHeight);
		CGContextDrawImage( mxTargetContext, aFullRect, xXorImage );
		CGImageRelease( xXorImage );
	}

	// reset the XorMask to black again
	// TODO: not needed for last update
	memset( mpMaskBuffer, 0, mnBufferLongs * sizeof(sal_uLong) );

	// TODO: return FALSE if target was not changed
	return true;
}

// =======================================================================
