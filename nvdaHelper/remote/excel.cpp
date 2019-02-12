/*
This file is a part of the NVDA project.
Copyright 2019 NV Access Limited.
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2.0, as published by
    the Free Software Foundation.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
This license can be found at:
http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
*/

#define WIN32_LEAN_AND_MEAN

#include <comdef.h>
#include <atlcomcli.h>
#include <windows.h>
#include <common/log.h>
#include <common/COMUtils.h>
#include "inProcess.h"
#include "nvdaInProcUtils.h"
#include "excel/constants.h"

long getCellTextWidth(HWND hwnd, IDispatch* pDispatchRange) {
	CComBSTR sText;
	// Fetch the text for this cell and work out its length in characters.
	HRESULT res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_TEXT,VT_BSTR,&sText);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"range.text failed with code "<<res);
		return 0;
	}
	long textLength=sText?sText.Length():0;
	if(textLength==0) {
		return 0;
	}
	// Fetch font size and weight information 
	CComPtr<IDispatch> pDispatchFont=nullptr;
	res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_FONT,VT_DISPATCH,&pDispatchFont);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"range.font failed with code "<<res);
		return 0;
	}
	double fontSize=11.0;
	res=_com_dispatch_raw_propget(pDispatchFont,XLDISPID_FONT_SIZE,VT_R8,&fontSize);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"font.size failed with code"<<res);
		return 0;
	}
	long iFontHeight=static_cast<long>(fontSize)*-1;
	BOOL bold=false;
	res=_com_dispatch_raw_propget(pDispatchFont,XLDISPID_FONT_BOLD,VT_BOOL,&bold);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"font.bold failed with code"<<res);
		return 0;
	}
	long iFontWeight=bold?700:400;
	BOOL sFontItalic=false;
	res=_com_dispatch_raw_propget(pDispatchFont,XLDISPID_FONT_ITALIC,VT_BOOL,&sFontItalic);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"font.italic failed with code"<<res);
		return 0;
	}
	long sFontUnderline=0;
	res=_com_dispatch_raw_propget(pDispatchFont,XLDISPID_FONT_UNDERLINE,VT_I4,&sFontUnderline);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"font.underline failed with code"<<res);
		return 0;
	}
	BOOL sFontStrikeThrough=false;
	res=_com_dispatch_raw_propget(pDispatchFont,XLDISPID_FONT_STRIKETHROUGH,VT_BOOL,&sFontStrikeThrough);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"font.strikethrough failed with code"<<res);
		return 0;
	}
	CComBSTR sFontName;
	res=_com_dispatch_raw_propget(pDispatchFont,XLDISPID_FONT_NAME,VT_BSTR,&sFontName);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"font.name failed with code"<<res);
		return 0;
	}
	// Create a memory device context compatible with the spreadsheet window
	HDC windowDC=GetDC(hwnd);
	HDC tempDC=CreateCompatibleDC(windowDC);
	ReleaseDC(hwnd,windowDC);
	HBITMAP hBmp=CreateCompatibleBitmap(tempDC,1,1);
	HGDIOBJ hOldBmp=SelectObject(tempDC,hBmp);
	// Create a GDI font object with all the font attributes fetched from Excel and load it into the device context. 
	long dpi = GetDeviceCaps(tempDC, LOGPIXELSX);
	long iFontWidth=0;
	long iEscapement=0;
	long iOrientation=0;
	long iCharSet=0;
	long iOutputPrecision=0;
	long iClipPrecision=0;
	long iOutputQuality=0;
	long iPitchAndFamily=0;
	HFONT hFont=CreateFontW(iFontHeight, iFontWidth, iEscapement, iOrientation, iFontWeight, sFontItalic, sFontUnderline, sFontStrikeThrough, iCharSet, iOutputPrecision, iClipPrecision, iOutputQuality, iPitchAndFamily, sFontName);
	HGDIOBJ hOldFont=SelectObject(tempDC, hFont);
	// have GDI calculate the size of the text in pixels, using the loaded font.
	SIZE sizl={0};
	GetTextExtentPoint32W(tempDC, sText, textLength,&sizl);
	// Clean up all the temporary GDI objects
	SelectObject(tempDC, hOldFont);
	DeleteObject(hFont);
	SelectObject(tempDC, hOldBmp);
	DeleteObject(hBmp);
	DeleteDC(tempDC);
	return sizl.cx;
}

__int64 getCellStates(HWND hwnd, IDispatch* pDispatchRange) {
	__int64 states=0;
	// If the current row is a summary row, expose the collapsed or expanded states depending on wither the inner rows are showing or not.
	CComPtr<IDispatch> pDispatchRow=nullptr;
	HRESULT res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_ENTIREROW,VT_DISPATCH,&pDispatchRow);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"range.entireRow failed with code "<<res);
	}
	if(pDispatchRow) {
		BOOL summary=false;
		res=_com_dispatch_raw_propget(pDispatchRow,XLDISPID_ROW_SUMMARY,VT_BOOL,&summary);
		if(FAILED(res)) {
			LOG_DEBUGWARNING(L"row.summary failed with code "<<res);
		}
		if(summary) {
			BOOL showDetail=false;
			res=_com_dispatch_raw_propget(pDispatchRow,XLDISPID_ROW_SHOWDETAIL,VT_BOOL,&showDetail);
			if(FAILED(res)) {
				LOG_DEBUGWARNING(L"row.showDetail failed with code "<<res);
			}
			states|=(showDetail?NVSTATE_EXPANDED:NVSTATE_COLLAPSED);
		}
	}
	// If this row was neither collapsed or expanded, then try the same for columns instead. 
	if(!(states&NVSTATE_EXPANDED)&&!(states&NVSTATE_COLLAPSED)) {
		CComPtr<IDispatch> pDispatchColumn=nullptr;
		res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_ENTIRECOLUMN,VT_DISPATCH,&pDispatchColumn);
		if(FAILED(res)) {
			LOG_DEBUGWARNING(L"range.entireColumn failed with code "<<res);
		}
		if(pDispatchColumn) {
			BOOL summary=false;
			res=_com_dispatch_raw_propget(pDispatchColumn,XLDISPID_COLUMN_SUMMARY,VT_BOOL,&summary);
			if(FAILED(res)) {
				LOG_DEBUGWARNING(L"column.summary failed with code "<<res);
			}
			if(summary) {
				BOOL showDetail=false;
				res=_com_dispatch_raw_propget(pDispatchColumn,XLDISPID_COLUMN_SHOWDETAIL,VT_BOOL,&showDetail);
				if(FAILED(res)) {
					LOG_DEBUGWARNING(L"column.showDetail failed with code "<<res);
				}
				states|=(showDetail?NVSTATE_EXPANDED:NVSTATE_COLLAPSED);
			}
		}
	}
	// Expose whether this cell has a formula
	BOOL hasFormula=false;
	res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_HASFORMULA,VT_BOOL,&hasFormula);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"range.hasFormula failed with code "<<res);
	}
	if(hasFormula) {
		states|=NVSTATE_HASFORMULA;
	}
	// Expose whether this cell has a dropdown menu for choosing valid values
	CComPtr<IDispatch> pDispatchValidation=nullptr;
	res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_VALIDATION,VT_DISPATCH,&pDispatchValidation);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"range.validation failed with code "<<res);
	}
	if(pDispatchValidation) {
		long validationType=0;
		res=_com_dispatch_raw_propget(pDispatchValidation,XLDISPID_VALIDATION_TYPE,VT_I4,&validationType);
		if(FAILED(res)) {
			//LOG_DEBUGWARNING(L"validation.type failed with code "<<res);
		}
		if(validationType==3) {
			states|=NVSTATE_HASPOPUP;
		}
	}
	// Expose whether this cell has comments
	CComPtr<IDispatch> pDispatchComment=nullptr;
	res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_COMMENT,VT_DISPATCH,&pDispatchComment);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"range.comment failed with code "<<res);
	}
	if(pDispatchComment) {
		states|=NVSTATE_HASCOMMENT;
	}
	// Expose whether this cell is unlocked for editing
	BOOL locked=false;
	res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_LOCKED,VT_BOOL,&locked);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"range.locked failed with code "<<res);
	}
	if(!locked) {
		CComPtr<IDispatch> pDispatchWorksheet=nullptr;
		res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_WORKSHEET,VT_DISPATCH,&pDispatchWorksheet);
		if(FAILED(res)) {
			LOG_DEBUGWARNING(L"range.worksheet failed with code "<<res);
		}
		if(pDispatchWorksheet) {
			BOOL protectContents=false;
			res=_com_dispatch_raw_propget(pDispatchWorksheet,XLDISPID_WORKSHEET_PROTECTCONTENTS,VT_BOOL,&protectContents);
			if(FAILED(res)) {
				LOG_DEBUGWARNING(L"worksheet.protectcontents failed with code "<<res);
			}
			if(protectContents) {
				states|=NVSTATE_UNLOCKED;
			}
		}
	}
	// Expose whether this cell contains links
	CComPtr<IDispatch> pDispatchHyperlinks=nullptr;
	res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_HYPERLINKS,VT_DISPATCH,&pDispatchHyperlinks);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"range.hyperlinks failed with code "<<res);
	}
	if(pDispatchHyperlinks) {
		long count=0;
		res=_com_dispatch_raw_propget(pDispatchHyperlinks,XLDISPID_HYPERLINKS_COUNT,VT_I4,&count);
		if(FAILED(res)) {
			LOG_DEBUGWARNING(L"hyperlinks.count failed with code "<<res);
		}
		if(count>0) {
			states|=NVSTATE_LINKED;
		}
	}
	// Expose whether this cell's content flows outside the cell, 
	// and if so, whether it is cropped by the next cell, or overflowing into the next cell. 
	BOOL shrinkToFit=false;
	res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_SHRINKTOFIT,VT_BOOL,&shrinkToFit);
	if(FAILED(res)) {
		LOG_DEBUGWARNING(L"range.shrinkToFit failed with code "<<res);
	}
	if(!shrinkToFit) {
		BOOL wrapText=false;
		res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_WRAPTEXT,VT_BOOL,&wrapText);
		if(FAILED(res)) {
			LOG_DEBUGWARNING(L"range.wrapText failed with code "<<res);
		}
		if(!wrapText) {
			long textWidth=getCellTextWidth(hwnd,pDispatchRange);
			if(textWidth>0) {
				long rangeWidth=0;
				CComPtr<IDispatch> pDispatchNextCell=nullptr;
				CComPtr<IDispatch> pDispatchMergeArea=nullptr;
				res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_MERGEAREA,VT_DISPATCH,&pDispatchMergeArea);
				if(FAILED(res)) {
					LOG_DEBUGWARNING(L"range.mergeArea failed with code "<<res);
				}
				if(pDispatchMergeArea) {
					res=_com_dispatch_raw_propget(pDispatchMergeArea,XLDISPID_RANGE_WIDTH,VT_I4,&rangeWidth);
					if(FAILED(res)) {
						LOG_DEBUGWARNING(L"range.width failed with code "<<res);
					}
					CComPtr<IDispatch> pDispatchColumns=nullptr;
					res=_com_dispatch_raw_propget(pDispatchMergeArea,XLDISPID_RANGE_COLUMNS,VT_DISPATCH,&pDispatchColumns);
					if(FAILED(res)) {
						LOG_DEBUGWARNING(L"range.columns failed with code "<<res);
					}
					if(pDispatchColumns) {
						long colCount=0;
						res=_com_dispatch_raw_propget(pDispatchColumns,XLDISPID_COLUMNS_COUNT,VT_I4,&colCount);
						if(FAILED(res)) {
							LOG_DEBUGWARNING(L"columns.count failed with code "<<res);
						}
						if(colCount>0) {
							CComPtr<IDispatch> pDispatchLastColumn=nullptr;
							res=_com_dispatch_raw_method(pDispatchColumns,XLDISPID_COLUMNS_ITEM,DISPATCH_PROPERTYGET,VT_DISPATCH,&pDispatchLastColumn,L"\x0003",colCount);
							if(FAILED(res)) {
								LOG_DEBUGWARNING(L"columns.item "<<colCount<<L" failed with code "<<res);
							}
							if(pDispatchLastColumn) {
								res=_com_dispatch_raw_propget(pDispatchLastColumn,XLDISPID_RANGE_NEXT,VT_DISPATCH,&pDispatchNextCell);
								if(FAILED(res)) {
									LOG_DEBUGWARNING(L"range.next failed with code "<<res);
								}
							}
						}
					}
				}
				if(rangeWidth==0) { // could not get width from a merge area
					res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_WIDTH,VT_I4,&rangeWidth);
					if(FAILED(res)) {
						LOG_DEBUGWARNING(L"range.width failed with code "<<res);
					}
				}
				if(textWidth>rangeWidth) {
					if(!pDispatchNextCell) {
						res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_NEXT,VT_DISPATCH,&pDispatchNextCell);
						if(FAILED(res)) {
							LOG_DEBUGWARNING(L"range.next failed with code "<<res);
						}
					}
					if(pDispatchNextCell) {
						CComBSTR text;
						res=_com_dispatch_raw_propget(pDispatchNextCell,XLDISPID_RANGE_TEXT,VT_BSTR,&text);
						if(FAILED(res)) {
							LOG_DEBUGWARNING(L"range.text failed with code "<<res);
						}
						if(text&&text.Length()>0) {
							states|=NVSTATE_CROPPED;
						}
					}
					if(!(states&NVSTATE_CROPPED)) {
						states|=NVSTATE_OVERFLOWING;
					}
				}
			}
		}
	}
	return states;
}

HRESULT getCellInfo(HWND hwnd, IDispatch* pDispatchRange, long cellInfoFlags, EXCEL_CELLINFO* cellInfo) {
	HRESULT res=S_OK;
	if(cellInfoFlags&NVCELLINFOFLAG_TEXT) {
		res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_TEXT,VT_BSTR,&cellInfo->text);
		if(FAILED(res)) {
			LOG_DEBUGWARNING(L"range.text failed with code "<<res);
		}
	}
	if(cellInfoFlags&NVCELLINFOFLAG_FORMULA) {
		res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_FORMULA,VT_BSTR,&cellInfo->formula);
		if(FAILED(res)) {
			LOG_DEBUGWARNING(L"range.formula failed with code "<<res);
		}
	}
	if(cellInfoFlags&NVCELLINFOFLAG_COMMENTS) {
		CComPtr<IDispatch> pDispatchComment=nullptr;
		res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_COMMENT,VT_DISPATCH,&pDispatchComment);
		if(FAILED(res)) {
			LOG_DEBUGWARNING(L"range.comment failed with code "<<res);
		}
		if(pDispatchComment) {
			res=_com_dispatch_raw_method(pDispatchComment,XLDISPID_COMMENT_TEXT,DISPATCH_METHOD,VT_BSTR,&cellInfo->comments,nullptr);
			if(FAILED(res)) {
				LOG_DEBUGWARNING(L"comment.text failed with code "<<res);
			}
		}
	}
	if(cellInfoFlags&NVCELLINFOFLAG_INPUTMESSAGE) {
		CComPtr<IDispatch> pDispatchValidation=nullptr;
		res=_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_VALIDATION,VT_DISPATCH,&pDispatchValidation);
		if(FAILED(res)) {
			LOG_DEBUGWARNING(L"range.validation failed with code "<<res);
		}
		if(pDispatchValidation) {
			_com_dispatch_raw_propget(pDispatchValidation,XLDISPID_VALIDATION_INPUTTITLE,VT_BSTR,&cellInfo->inputTitle);
			_com_dispatch_raw_propget(pDispatchValidation,XLDISPID_VALIDATION_INPUTMESSAGE,VT_BSTR,&cellInfo->inputMessage);
		}
	}
	if(cellInfoFlags&NVCELLINFOFLAG_STATES) {
		cellInfo->states=getCellStates(hwnd,pDispatchRange);
	}
	CComPtr<IDispatch> pDispatchMergeArea=nullptr;
	if(cellInfoFlags&NVCELLINFOFLAG_COORDS||cellInfoFlags&NVCELLINFOFLAG_OUTLINELEVEL) {
		_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_MERGEAREA,VT_DISPATCH,&pDispatchMergeArea);
	}
	if(cellInfoFlags&NVCELLINFOFLAG_COORDS) {
		_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_ROW,VT_I4,&cellInfo->rowNumber);
		_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_COLUMN,VT_I4,&cellInfo->columnNumber);
	}
	if(cellInfoFlags&NVCELLINFOFLAG_ADDRESS) {
		if(pDispatchMergeArea) {
			_com_dispatch_raw_method(pDispatchMergeArea,XLDISPID_RANGE_ADDRESS,DISPATCH_PROPERTYGET,VT_BSTR,&cellInfo->address,L"\x000b\x000b\x0003\x000b",false,false,1,true);
		} else {
			_com_dispatch_raw_method(pDispatchRange,XLDISPID_RANGE_ADDRESS,DISPATCH_PROPERTYGET,VT_BSTR,&cellInfo->address,L"\x000b\x000b\x0003\x000b",false,false,1,true);
		}
	}
	if(cellInfoFlags&NVCELLINFOFLAG_COORDS&&pDispatchMergeArea) {
		_com_dispatch_raw_method(pDispatchMergeArea,XLDISPID_RANGE_ADDRESS,DISPATCH_PROPERTYGET,VT_BSTR,&cellInfo->address,L"\x000b\x000b\x0003\x000b",false,false,1,true);
		CComPtr<IDispatch> pDispatchRows=nullptr;
		_com_dispatch_raw_propget(pDispatchMergeArea,XLDISPID_RANGE_ROWS,VT_DISPATCH,&pDispatchRows);
		if(pDispatchRows) {
			_com_dispatch_raw_propget(pDispatchRows,XLDISPID_ROWS_COUNT,VT_I4,&cellInfo->rowSpan);
		}
		CComPtr<IDispatch> pDispatchColumns=nullptr;
		_com_dispatch_raw_propget(pDispatchMergeArea,XLDISPID_RANGE_COLUMNS,VT_DISPATCH,&pDispatchColumns);
		if(pDispatchColumns) {
			_com_dispatch_raw_propget(pDispatchColumns,XLDISPID_COLUMNS_COUNT,VT_I4,&cellInfo->columnSpan);
		}
	}
	if(cellInfoFlags&NVCELLINFOFLAG_OUTLINELEVEL) {
		CComPtr<IDispatch> pDispatchRow=nullptr;
		_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_ENTIREROW,VT_DISPATCH,&pDispatchRow);
		if(pDispatchRow) {
			_com_dispatch_raw_propget(pDispatchRow,XLDISPID_ROW_OUTLINELEVEL,VT_I4,&cellInfo->outlineLevel);
		}
		if(cellInfo->outlineLevel==0) {
			CComPtr<IDispatch> pDispatchColumn=nullptr;
			_com_dispatch_raw_propget(pDispatchRange,XLDISPID_RANGE_ENTIRECOLUMN,VT_DISPATCH,&pDispatchColumn);
			if(pDispatchColumn) {
				_com_dispatch_raw_propget(pDispatchColumn,XLDISPID_COLUMN_OUTLINELEVEL,VT_I4,&cellInfo->outlineLevel);
			}
		}
	}
	return RPC_S_OK;
}

error_status_t nvdaInProcUtils_excel_getCellInfos(handle_t bindingHandle, const unsigned long windowHandle, IDispatch* arg_pDispatchRange, long cellInfoFlags, long cellCount, EXCEL_CELLINFO* cellInfos, long* numCellsFetched) {
	HWND hwnd=static_cast<HWND>(UlongToHandle(windowHandle));
	long threadID=GetWindowThreadProcessId(hwnd,nullptr);
	nvCOMUtils::InterfaceMarshaller im;
	HRESULT res=im.marshal(arg_pDispatchRange);
	if(FAILED(res)) {
		LOG_ERROR(L"Failed to marshal range object from rpc thread");
		return E_UNEXPECTED;
	}
	// Execute the following code in Excel's GUI thread. 
	execInThread(threadID,[&](){
		// Unmarshal the IDispatch pointer from the COM global interface table.
		CComPtr<IDispatch> pDispatchRange=im.unmarshal<IDispatch>();
		if(!pDispatchRange) {
			LOG_ERROR(L"Failed to unmarshal range object into Excel GUI thread");
			return;
		}
		if(cellCount==1) {
			getCellInfo(hwnd,pDispatchRange,cellInfoFlags, cellInfos);
			*numCellsFetched=1;
		} else for(long i=0;i<cellCount;++i) {
			CComPtr<IDispatch> pDispatchCell=nullptr;
			res=_com_dispatch_raw_method(pDispatchRange,XLDISPID_RANGE_ITEM,DISPATCH_PROPERTYGET,VT_DISPATCH,&pDispatchCell,L"\x0003",i);
			if(FAILED(res)) {
				LOG_DEBUGWARNING(L"range.item "<<i<<L" failed with code "<<res);
				break;
			}
			getCellInfo(hwnd,pDispatchCell,cellInfoFlags,cellInfos+i);
			*numCellsFetched=i+1;
		}
	});
	return RPC_S_OK;
}
