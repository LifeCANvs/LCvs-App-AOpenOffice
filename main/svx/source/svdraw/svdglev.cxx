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
#include "precompiled_svx.hxx"

#include <svx/svdglev.hxx>
#include <math.h>

#include <svx/svdundo.hxx>
#include "svx/svdstr.hrc" // Namen aus der Ressource
#include "svx/svdglob.hxx" // StringCache
#include <svx/svdpagv.hxx>
#include <svx/svdglue.hxx>
#include <svx/svdtrans.hxx>
#include <svx/svdobj.hxx>


void SdrGlueEditView::ImpClearVars()
{
}

SdrGlueEditView::SdrGlueEditView(SdrModel* pModel1, OutputDevice* pOut):
	SdrPolyEditView(pModel1,pOut)
{
	ImpClearVars();
}

SdrGlueEditView::~SdrGlueEditView()
{
}


void SdrGlueEditView::ImpDoMarkedGluePoints(PGlueDoFunc pDoFunc, sal_Bool bConst, const void* p1, const void* p2, const void* p3, const void* p4, const void* p5)
{
	sal_uIntPtr nMarkAnz=GetMarkedObjectCount();
	for (sal_uIntPtr nm=0; nm<nMarkAnz; nm++) {
		SdrMark* pM=GetSdrMarkByIndex(nm);
		SdrObject* pObj=pM->GetMarkedSdrObj();
		const SdrUShortCont* pPts=pM->GetMarkedGluePoints();
		sal_uIntPtr nPtAnz=pPts==NULL ? 0 : pPts->GetCount();
		if (nPtAnz!=0) {
			SdrGluePointList* pGPL=NULL;
			if (bConst) {
				const SdrGluePointList* pConstGPL=pObj->GetGluePointList();
				pGPL=(SdrGluePointList*)pConstGPL;
			} else {
				pGPL=pObj->ForceGluePointList();
			}
			if (pGPL!=NULL)
			{
				if(!bConst && IsUndoEnabled() )
					AddUndo(GetModel()->GetSdrUndoFactory().CreateUndoGeoObject(*pObj));

				for (sal_uIntPtr nPtNum=0; nPtNum<nPtAnz; nPtNum++)
				{
					sal_uInt16 nPtId=pPts->GetObject(nPtNum);
					sal_uInt16 nGlueIdx=pGPL->FindGluePoint(nPtId);
					if (nGlueIdx!=SDRGLUEPOINT_NOTFOUND)
					{
						SdrGluePoint& rGP=(*pGPL)[nGlueIdx];
						(*pDoFunc)(rGP,pObj,p1,p2,p3,p4,p5);
					}
				}
				if (!bConst)
				{
					pObj->SetChanged();
					pObj->BroadcastObjectChange();
				}
			}
		}
	}
	if (!bConst && nMarkAnz!=0) pMod->SetChanged();
}


static void ImpGetEscDir(SdrGluePoint& rGP, const SdrObject* /*pObj*/, const void* pbFirst, const void* pnThisEsc, const void* pnRet, const void*, const void*)
{
	sal_uInt16& nRet=*(sal_uInt16*)pnRet;
	sal_Bool& bFirst=*(sal_Bool*)pbFirst;
	if (nRet!=FUZZY) {
		sal_uInt16 nEsc=rGP.GetEscDir();
		sal_Bool bOn=(nEsc & *(sal_uInt16*)pnThisEsc)!=0;
		if (bFirst) { nRet=bOn; bFirst=sal_False; }
		else if (nRet!=bOn) nRet=FUZZY;
	}
}

TRISTATE SdrGlueEditView::IsMarkedGluePointsEscDir(sal_uInt16 nThisEsc) const
{
	ForceUndirtyMrkPnt();
	sal_Bool bFirst=sal_True;
	sal_uInt16 nRet=sal_False;
	((SdrGlueEditView*)this)->ImpDoMarkedGluePoints(ImpGetEscDir,sal_True,&bFirst,&nThisEsc,&nRet);
	return (TRISTATE)nRet;
}

static void ImpSetEscDir(SdrGluePoint& rGP, const SdrObject* /*pObj*/, const void* pnThisEsc, const void* pbOn, const void*, const void*, const void*)
{
	sal_uInt16 nEsc=rGP.GetEscDir();
	if (*(sal_Bool*)pbOn) nEsc|=*(sal_uInt16*)pnThisEsc;
	else nEsc&=~*(sal_uInt16*)pnThisEsc;
	rGP.SetEscDir(nEsc);
}

void SdrGlueEditView::SetMarkedGluePointsEscDir(sal_uInt16 nThisEsc, sal_Bool bOn)
{
	ForceUndirtyMrkPnt();
	BegUndo(ImpGetResStr(STR_EditSetGlueEscDir),GetDescriptionOfMarkedGluePoints());
	ImpDoMarkedGluePoints(ImpSetEscDir,sal_False,&nThisEsc,&bOn);
	EndUndo();
}


static void ImpGetPercent(SdrGluePoint& rGP, const SdrObject* /*pObj*/, const void* pbFirst, const void* pnRet, const void*, const void*, const void*)
{
	sal_uInt16& nRet=*(sal_uInt16*)pnRet;
	sal_Bool& bFirst=*(sal_Bool*)pbFirst;
	if (nRet!=FUZZY) {
		bool bOn=rGP.IsPercent();
		if (bFirst) { nRet=bOn; bFirst=sal_False; }
		else if ((nRet!=0)!=bOn) nRet=FUZZY;
	}
}

TRISTATE SdrGlueEditView::IsMarkedGluePointsPercent() const
{
	ForceUndirtyMrkPnt();
	sal_Bool bFirst=sal_True;
	sal_uInt16 nRet=sal_True;
	((SdrGlueEditView*)this)->ImpDoMarkedGluePoints(ImpGetPercent,sal_True,&bFirst,&nRet);
	return (TRISTATE)nRet;
}

static void ImpSetPercent(SdrGluePoint& rGP, const SdrObject* pObj, const void* pbOn, const void*, const void*, const void*, const void*)
{
	Point aPos(rGP.GetAbsolutePos(*pObj));
	rGP.SetPercent(*(sal_Bool*)pbOn);
	rGP.SetAbsolutePos(aPos,*pObj);
}

void SdrGlueEditView::SetMarkedGluePointsPercent(sal_Bool bOn)
{
	ForceUndirtyMrkPnt();
	BegUndo(ImpGetResStr(STR_EditSetGluePercent),GetDescriptionOfMarkedGluePoints());
	ImpDoMarkedGluePoints(ImpSetPercent,sal_False,&bOn);
	EndUndo();
}


static void ImpGetAlign(SdrGluePoint& rGP, const SdrObject* /*pObj*/, const void* pbFirst, const void* pbDontCare, const void* pbVert, const void* pnRet, const void*)
{
	sal_uInt16& nRet=*(sal_uInt16*)pnRet;
	sal_Bool& bFirst=*(sal_Bool*)pbFirst;
	sal_Bool& bDontCare=*(sal_Bool*)pbDontCare;
	sal_Bool bVert=*(sal_Bool*)pbVert;
	if (!bDontCare) {
		sal_uInt16 nAlg=0;
		if (bVert) {
			nAlg=rGP.GetVertAlign();
		} else {
			nAlg=rGP.GetHorzAlign();
		}
		if (bFirst) { nRet=nAlg; bFirst=sal_False; }
		else if (nRet!=nAlg) {
			if (bVert) {
				nRet=SDRVERTALIGN_DONTCARE;
			} else {
				nRet=SDRHORZALIGN_DONTCARE;
			}
			bDontCare=sal_True;
		}
	}
}

sal_uInt16 SdrGlueEditView::GetMarkedGluePointsAlign(sal_Bool bVert) const
{
	ForceUndirtyMrkPnt();
	sal_Bool bFirst=sal_True;
	sal_Bool bDontCare=sal_False;
	sal_uInt16 nRet=0;
	((SdrGlueEditView*)this)->ImpDoMarkedGluePoints(ImpGetAlign,sal_True,&bFirst,&bDontCare,&bVert,&nRet);
	return nRet;
}

static void ImpSetAlign(SdrGluePoint& rGP, const SdrObject* pObj, const void* pbVert, const void* pnAlign, const void*, const void*, const void*)
{
	Point aPos(rGP.GetAbsolutePos(*pObj));
	if (*(sal_Bool*)pbVert) { // bVert?
		rGP.SetVertAlign(*(sal_uInt16*)pnAlign);
	} else {
		rGP.SetHorzAlign(*(sal_uInt16*)pnAlign);
	}
	rGP.SetAbsolutePos(aPos,*pObj);
}

void SdrGlueEditView::SetMarkedGluePointsAlign(sal_Bool bVert, sal_uInt16 nAlign)
{
	ForceUndirtyMrkPnt();
	BegUndo(ImpGetResStr(STR_EditSetGlueAlign),GetDescriptionOfMarkedGluePoints());
	ImpDoMarkedGluePoints(ImpSetAlign,sal_False,&bVert,&nAlign);
	EndUndo();
}


sal_Bool SdrGlueEditView::IsDeleteMarkedGluePointsPossible() const
{
	return HasMarkedGluePoints();
}

void SdrGlueEditView::DeleteMarkedGluePoints()
{
	BrkAction();
	ForceUndirtyMrkPnt();
	const bool bUndo = IsUndoEnabled();
	if( bUndo )
		BegUndo(ImpGetResStr(STR_EditDelete),GetDescriptionOfMarkedGluePoints(),SDRREPFUNC_OBJ_DELETE);

	sal_uIntPtr nMarkAnz=GetMarkedObjectCount();
	for (sal_uIntPtr nm=0; nm<nMarkAnz; nm++)
	{
		SdrMark* pM=GetSdrMarkByIndex(nm);
		SdrObject* pObj=pM->GetMarkedSdrObj();
		const SdrUShortCont* pPts=pM->GetMarkedGluePoints();
		sal_uIntPtr nPtAnz=pPts==NULL ? 0 : pPts->GetCount();
		if (nPtAnz!=0)
		{
			SdrGluePointList* pGPL=pObj->ForceGluePointList();
			if (pGPL!=NULL)
			{
				if( bUndo )
					AddUndo(GetModel()->GetSdrUndoFactory().CreateUndoGeoObject(*pObj));

				for (sal_uIntPtr nPtNum=0; nPtNum<nPtAnz; nPtNum++)
				{
					sal_uInt16 nPtId=pPts->GetObject(nPtNum);
					sal_uInt16 nGlueIdx=pGPL->FindGluePoint(nPtId);
					if (nGlueIdx!=SDRGLUEPOINT_NOTFOUND)
					{
						pGPL->Delete(nGlueIdx);
					}
				}
				pObj->SetChanged();
				pObj->BroadcastObjectChange();
			}
		}
	}
	if( bUndo )
		EndUndo();
	UnmarkAllGluePoints();
	if (nMarkAnz!=0)
		pMod->SetChanged();
}


void SdrGlueEditView::ImpCopyMarkedGluePoints()
{
	const bool bUndo = IsUndoEnabled();

	if( bUndo )
		BegUndo();

	sal_uIntPtr nMarkAnz=GetMarkedObjectCount();
	for (sal_uIntPtr nm=0; nm<nMarkAnz; nm++)
	{
		SdrMark* pM=GetSdrMarkByIndex(nm);
		SdrObject* pObj=pM->GetMarkedSdrObj();
		SdrUShortCont* pPts=pM->GetMarkedGluePoints();
		SdrGluePointList* pGPL=pObj->ForceGluePointList();
		sal_uIntPtr nPtAnz=pPts==NULL ? 0 : pPts->GetCount();
		if (nPtAnz!=0 && pGPL!=NULL)
		{
			if( bUndo )
				AddUndo(GetModel()->GetSdrUndoFactory().CreateUndoGeoObject(*pObj));

			for (sal_uIntPtr nPtNum=0; nPtNum<nPtAnz; nPtNum++)
			{
				sal_uInt16 nPtId=pPts->GetObject(nPtNum);
				sal_uInt16 nGlueIdx=pGPL->FindGluePoint(nPtId);
				if (nGlueIdx!=SDRGLUEPOINT_NOTFOUND)
				{
					SdrGluePoint aNewGP((*pGPL)[nGlueIdx]); // Clone GluePoint
					sal_uInt16 nNewIdx=pGPL->Insert(aNewGP); // and insert
					sal_uInt16 nNewId=(*pGPL)[nNewIdx].GetId(); // Id des neuen GluePoints ermitteln
					pPts->Replace(nNewId,nPtNum); // und diesen markieren (anstelle des alten)
				}
			}
		}
	}
	if( bUndo )
		EndUndo();

	if (nMarkAnz!=0)
		pMod->SetChanged();
}


void SdrGlueEditView::ImpTransformMarkedGluePoints(PGlueTrFunc pTrFunc, const void* p1, const void* p2, const void* p3, const void* p4, const void* p5)
{
	sal_uIntPtr nMarkAnz=GetMarkedObjectCount();
	for (sal_uIntPtr nm=0; nm<nMarkAnz; nm++) {
		SdrMark* pM=GetSdrMarkByIndex(nm);
		SdrObject* pObj=pM->GetMarkedSdrObj();
		const SdrUShortCont* pPts=pM->GetMarkedGluePoints();
		sal_uIntPtr nPtAnz=pPts==NULL ? 0 : pPts->GetCount();
		if (nPtAnz!=0) {
			SdrGluePointList* pGPL=pObj->ForceGluePointList();
			if (pGPL!=NULL)
			{
				if( IsUndoEnabled() )
					AddUndo(GetModel()->GetSdrUndoFactory().CreateUndoGeoObject(*pObj));

				for (sal_uIntPtr nPtNum=0; nPtNum<nPtAnz; nPtNum++) {
					sal_uInt16 nPtId=pPts->GetObject(nPtNum);
					sal_uInt16 nGlueIdx=pGPL->FindGluePoint(nPtId);
					if (nGlueIdx!=SDRGLUEPOINT_NOTFOUND) {
						SdrGluePoint& rGP=(*pGPL)[nGlueIdx];
						Point aPos(rGP.GetAbsolutePos(*pObj));
						(*pTrFunc)(aPos,p1,p2,p3,p4,p5);
						rGP.SetAbsolutePos(aPos,*pObj);
					}
				}
				pObj->SetChanged();
				pObj->BroadcastObjectChange();
			}
		}
	}
	if (nMarkAnz!=0) pMod->SetChanged();
}


static void ImpMove(Point& rPt, const void* p1, const void* /*p2*/, const void* /*p3*/, const void* /*p4*/, const void* /*p5*/)
{
	rPt.X()+=((const Size*)p1)->Width();
	rPt.Y()+=((const Size*)p1)->Height();
}

void SdrGlueEditView::MoveMarkedGluePoints(const Size& rSiz, bool bCopy)
{
	ForceUndirtyMrkPnt();
	XubString aStr(ImpGetResStr(STR_EditMove));
	if (bCopy) aStr+=ImpGetResStr(STR_EditWithCopy);
	BegUndo(aStr,GetDescriptionOfMarkedGluePoints(),SDRREPFUNC_OBJ_MOVE);
	if (bCopy) ImpCopyMarkedGluePoints();
	ImpTransformMarkedGluePoints(ImpMove,&rSiz);
	EndUndo();
	AdjustMarkHdl();
}


static void ImpResize(Point& rPt, const void* p1, const void* p2, const void* p3, const void* /*p4*/, const void* /*p5*/)
{
	ResizePoint(rPt,*(const Point*)p1,*(const Fraction*)p2,*(const Fraction*)p3);
}

void SdrGlueEditView::ResizeMarkedGluePoints(const Point& rRef, const Fraction& xFact, const Fraction& yFact, bool bCopy)
{
	ForceUndirtyMrkPnt();
	XubString aStr(ImpGetResStr(STR_EditResize));
	if (bCopy) aStr+=ImpGetResStr(STR_EditWithCopy);
	BegUndo(aStr,GetDescriptionOfMarkedGluePoints(),SDRREPFUNC_OBJ_RESIZE);
	if (bCopy) ImpCopyMarkedGluePoints();
	ImpTransformMarkedGluePoints(ImpResize,&rRef,&xFact,&yFact);
	EndUndo();
	AdjustMarkHdl();
}


static void ImpRotate(Point& rPt, const void* p1, const void* /*p2*/, const void* p3, const void* p4, const void* /*p5*/)
{
	RotatePoint(rPt,*(const Point*)p1,*(const double*)p3,*(const double*)p4);
}

void SdrGlueEditView::RotateMarkedGluePoints(const Point& rRef, long nWink, bool bCopy)
{
	ForceUndirtyMrkPnt();
	XubString aStr(ImpGetResStr(STR_EditRotate));
	if (bCopy) aStr+=ImpGetResStr(STR_EditWithCopy);
	BegUndo(aStr,GetDescriptionOfMarkedGluePoints(),SDRREPFUNC_OBJ_ROTATE);
	if (bCopy) ImpCopyMarkedGluePoints();
	double nSin=sin(nWink*nPi180);
	double nCos=cos(nWink*nPi180);
	ImpTransformMarkedGluePoints(ImpRotate,&rRef,&nWink,&nSin,&nCos);
	EndUndo();
	AdjustMarkHdl();
}

/* vim: set noet sw=4 ts=4: */
