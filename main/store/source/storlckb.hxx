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



#ifndef _STORE_STORLCKB_HXX_
#define _STORE_STORLCKB_HXX_ "$Revision$"

#include "sal/types.h"

#include "rtl/ustring.h"
#include "rtl/ref.hxx"

#include "object.hxx"
#include "storbase.hxx"
#include "storpage.hxx"

namespace store
{

struct OStoreDataPageData;
struct OStoreDirectoryPageData;

/*========================================================================
 * OStoreLockBytes interface.
 *======================================================================*/
class OStoreLockBytes : public store::OStoreObject
{
public:
	/** Construction.
	 */
	OStoreLockBytes (void);

	/** create (two-phase construction).
	 *  @param  pManager [in]
	 *  @param  pPath [in]
	 *  @param  pName [in]
	 *  @param  eMode [in]
	 *  @return store_E_None upon success
	 */
	storeError create (
		OStorePageManager *pManager,
		rtl_String        *pPath,
		rtl_String        *pName,
		storeAccessMode    eAccessMode);

	/** Read at Offset into Buffer.
	 *  @param  nOffset [in]
	 *  @param  pBuffer [out]
	 *  @param  nBytes [in]
	 *  @param  rnDone [out]
	 *  @return store_E_None upon success
	 */
	storeError readAt (
		sal_uInt32  nOffset,
		void       *pBuffer,
		sal_uInt32  nBytes,
		sal_uInt32 &rnDone);

	/** Write at Offset from Buffer.
	 *  @param  nOffset [in]
	 *  @param  pBuffer [in]
	 *  @param  nBytes [in]
	 *  @param  rnDone [out]
	 *  @return store_E_None upon success
	 */
	storeError writeAt (
		sal_uInt32  nOffset,
		const void *pBuffer,
		sal_uInt32  nBytes,
		sal_uInt32 &rnDone);

	/** flush.
	 *  @return store_E_None upon success
	 */
	storeError flush (void);

	/** setSize.
	 *  @param  nSize [in]
	 *  @return store_E_None upon success
	 */
	storeError setSize (sal_uInt32 nSize);

	/** stat.
	 *  @param  rnSize [out]
	 *  @return store_E_None upon success
	 */
	storeError stat (sal_uInt32 &rnSize);

	/** IStoreHandle.
	 */
	virtual sal_Bool SAL_CALL isKindOf (sal_uInt32 nMagic);

protected:
	/** Destruction (OReference).
	 */
	virtual ~OStoreLockBytes (void);

private:
	/** IStoreHandle TypeId.
	 */
	static const sal_uInt32 m_nTypeId;

	/** IStoreHandle query() template specialization.
	 */
	friend OStoreLockBytes*
	SAL_CALL query<> (IStoreHandle *pHandle, OStoreLockBytes*);

	/** Representation.
	 */
	rtl::Reference<OStorePageManager> m_xManager;

	typedef OStoreDataPageData        data;
	typedef OStoreDirectoryPageData   inode;

	typedef PageHolderObject< inode > inode_holder_type;
	inode_holder_type                 m_xNode;

	bool m_bWriteable;

	/** Not implemented.
	 */
	OStoreLockBytes (const OStoreLockBytes&);
	OStoreLockBytes& operator= (const OStoreLockBytes&);
};

template<> inline OStoreLockBytes*
SAL_CALL query (IStoreHandle *pHandle, OStoreLockBytes*)
{
	if (pHandle && pHandle->isKindOf (OStoreLockBytes::m_nTypeId))
	{
		// Handle is kind of OStoreLockBytes.
		return static_cast<OStoreLockBytes*>(pHandle);
	}
	return 0;
}

} // namespace store

#endif /* !_STORE_STORLCKB_HXX_ */

/* vim: set noet sw=4 ts=4: */
