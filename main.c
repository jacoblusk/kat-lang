#define WIN32_LEAN_AND_MEAN
#define COBJMACROS

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

#include <windows.h>
#include <shlwapi.h>

#define KAT_NUMBER_BUFFER_SIZE 128

const char *src = "(\"Hello\") 12345 5.6 \nabc";

typedef enum {
	KAT_TOKEN_TYPE_IDENTIFIER,
	KAT_TOKEN_TYPE_INTEGER,
	KAT_TOKEN_TYPE_FLOAT,
	KAT_TOKEN_TYPE_STRING,
	KAT_TOKEN_TYPE_LPAREN,
	KAT_TOKEN_TYPE_RPAREN
} KAT_TOKEN_TYPE;

typedef struct {
	SIZE_T cLine;
	SIZE_T cCol;
} KAT_CURSOR;

typedef struct {
	KAT_TOKEN_TYPE tokenType;
	KAT_CURSOR cursor;
	union {
		int iVal;
		double fVal;
		const char *pszVal;
	};
} KAT_TOKEN, *PKAT_TOKEN;

typedef struct {
	IStream *pStream;
	KAT_CURSOR cursor;
} KAT_LEXER, *PKAT_LEXER;

HRESULT KAT_LexerNumber(PKAT_LEXER pLexer, _Out_ PKAT_TOKEN pToken);
HRESULT KAT_LexerString(PKAT_LEXER pLexer, _Out_ PKAT_TOKEN pToken);
HRESULT KAT_LexerIdentifier(PKAT_LEXER pLexer, _Out_ PKAT_TOKEN pToken);

HRESULT IStream_Peek(IStream *pStream, VOID *pVoid, size_t cAmount) {
	HRESULT hResult;
	hResult = IStream_Read(pStream, pVoid, cAmount);
	if(FAILED(hResult))
		return hResult;

	hResult = IStream_Seek(pStream, (LARGE_INTEGER){-1 * cAmount}, STREAM_SEEK_CUR, NULL);
	if(FAILED(hResult))
		return hResult;

	return S_OK;
}

HRESULT KAT_LexerNext(PKAT_LEXER pLexer, _Out_ PKAT_TOKEN pToken) {
	CHAR c;
	HRESULT hResult;

	do {
		hResult = IStream_Read(pLexer->pStream, &c, 1);
		if(FAILED(hResult))
			return hResult;

		if(c == '\n') {
			pLexer->cursor.cLine++;
			pLexer->cursor.cCol = 0;
			continue;
		}

		if(!isspace(c)) {
			hResult = IStream_Seek(pLexer->pStream, (LARGE_INTEGER){-1}, STREAM_SEEK_CUR, NULL);
			if(FAILED(hResult))
				return hResult;
			break;
		} else {
			pLexer->cursor.cCol++;
		}
	} while(1);

	if(isdigit(c)) {
		return KAT_LexerNumber(pLexer, pToken);
	} else if(isalpha(c)) {
		return KAT_LexerIdentifier(pLexer, pToken);
	}

	switch(c) {
		case '(': {
			hResult = IStream_Read(pLexer->pStream, &c, 1);
			if(FAILED(hResult))
				return hResult;

			pToken->tokenType = KAT_TOKEN_TYPE_LPAREN;
			pLexer->cursor.cCol++;
			break;
		}
		case ')': {
			hResult = IStream_Read(pLexer->pStream, &c, 1);
			if(FAILED(hResult))
				return hResult;

			pToken->tokenType = KAT_TOKEN_TYPE_RPAREN;
			pLexer->cursor.cCol++;
			break;
		}
		case '\"': {
			return KAT_LexerString(pLexer, pToken);
			break;
		}
		default:
			return E_NOTIMPL;
	}

	return S_OK;
}

//TODO: Move to a single loop.
HRESULT KAT_LexerNumber(PKAT_LEXER pLexer, _Out_ PKAT_TOKEN pToken) {
	_Bool bFloat = 0;
	_Bool bEOF = 0;
	SIZE_T uNumberLength = 0;
	CHAR c;
	CHAR sBuffer[KAT_NUMBER_BUFFER_SIZE];
	HRESULT hResult;

	do {
		hResult = IStream_Read(pLexer->pStream, &c, 1);
		if(FAILED(hResult)) {
			bEOF = 1;
			break;
		}

		if(c == '.' && !bFloat) {
			bFloat = 1;
		} else if(!isdigit(c)) {
			if(c != '.' || (c == '.' && bFloat)) {
				break;
			}
		}

		uNumberLength++;
	} while(1);

	if(uNumberLength > KAT_NUMBER_BUFFER_SIZE)
		return E_FAIL;

	IStream_Seek(pLexer->pStream, (LARGE_INTEGER) { -1 * (uNumberLength + (bEOF ? 0 : 1))}, STREAM_SEEK_CUR, NULL);

	sBuffer[uNumberLength] = '\0';
	hResult = IStream_Read(pLexer->pStream, sBuffer, uNumberLength);
	if(FAILED(hResult)) {
		return hResult;
	}

	if(bFloat) {
		pToken->tokenType = KAT_TOKEN_TYPE_FLOAT;
		pToken->fVal = atof(sBuffer);
	}
	else {
		pToken->tokenType = KAT_TOKEN_TYPE_INTEGER;
		pToken->iVal = atol(sBuffer);
	}

	pToken->cursor = pLexer->cursor;
	pLexer->cursor.cCol += uNumberLength;

	return S_OK;
}

//TODO: Move to a single loop.
HRESULT KAT_LexerString(PKAT_LEXER pLexer, _Out_ PKAT_TOKEN pToken) {
	_Bool bEscape = 0;
	_Bool bEOF = 0;
	SIZE_T uStringLength = 0;
	CHAR c;
	CHAR *sBuffer = NULL;
	HRESULT hResult;

	//Consume first double-quote.
	hResult = IStream_Read(pLexer->pStream, &c, 1);
	assert(SUCCEEDED(hResult));

	do {
		hResult = IStream_Read(pLexer->pStream, &c, 1);
		if(FAILED(hResult)) {
			return E_FAIL;
		}

		if(c == '\"' && !bEscape) {
			break;
		} 

		if(c == '\"' && bEscape) {
			bEscape = 0;
		}

		if(bEscape) {
			return E_FAIL;
		}

		if(c == '\n')
			return E_FAIL;

		else if(c == '\\')
			bEscape = 1;

		uStringLength++;
	} while(1);

	hResult = IStream_Seek(pLexer->pStream, (LARGE_INTEGER) { -1 * (uStringLength + (bEOF ? 0 : 1))}, STREAM_SEEK_CUR, NULL);
	if(FAILED(hResult))
		return hResult;

	sBuffer = malloc(sizeof(CHAR) * (uStringLength + 1));
	if(!sBuffer)
		return E_OUTOFMEMORY;

	sBuffer[uStringLength] = '\0';
	hResult = IStream_Read(pLexer->pStream, sBuffer, uStringLength);
	if(FAILED(hResult)) {
		free(sBuffer);
		return hResult;
	}

	hResult = IStream_Read(pLexer->pStream, &c, 1);
	if(FAILED(hResult))
		return hResult;

	pToken->tokenType = KAT_TOKEN_TYPE_STRING;
	pToken->pszVal = sBuffer;
	pToken->cursor = pLexer->cursor;

	pLexer->cursor.cCol += uStringLength + 2;
	return S_OK;
}

HRESULT KAT_LexerIdentifier(PKAT_LEXER pLexer, _Out_ PKAT_TOKEN pToken) {
	_Bool bEOF = 0;
	SIZE_T uStringLength = 0;
	CHAR c;
	CHAR *sBuffer = NULL;
	HRESULT hResult;

	do {
		hResult = IStream_Read(pLexer->pStream, &c, 1);
		if(FAILED(hResult)) {
			bEOF = 1;
			break;
		}

		if(!isalpha(c))
			break;

		uStringLength++;
	} while(1);

	hResult = IStream_Seek(pLexer->pStream, (LARGE_INTEGER) { -1 * (uStringLength + (bEOF ? 0 : 1))}, STREAM_SEEK_CUR, NULL);
	if(FAILED(hResult))
		return hResult;

	sBuffer = malloc(sizeof(CHAR) * (uStringLength + 1));
	if(!sBuffer)
		return E_OUTOFMEMORY;

	sBuffer[uStringLength] = '\0';
	hResult = IStream_Read(pLexer->pStream, sBuffer, uStringLength);
	if(FAILED(hResult)) {
		free(sBuffer);
		return hResult;
	}

	pToken->tokenType = KAT_TOKEN_TYPE_IDENTIFIER;
	pToken->pszVal = sBuffer;
	pToken->cursor = pLexer->cursor;

	pLexer->cursor.cCol += uStringLength;

	return S_OK;
}

int main(int argc, char **argv) {
	IStream *pStream = NULL;

	pStream = SHCreateMemStream((const BYTE *)src, strlen(src));
	KAT_LEXER lexer = (KAT_LEXER) { pStream, (KAT_CURSOR) {0, 0} };
	KAT_TOKEN token;

	while(SUCCEEDED(KAT_LexerNext(&lexer, &token))) {
		switch(token.tokenType) {
			case KAT_TOKEN_TYPE_STRING: {
				printf("[S %s (%llu, %llu)] ", token.pszVal, token.cursor.cLine, token.cursor.cCol);
				break;
			}
			case KAT_TOKEN_TYPE_IDENTIFIER: {
				printf("[ID %s (%llu, %llu)] ", token.pszVal, token.cursor.cLine, token.cursor.cCol);
				break;
			}
			case KAT_TOKEN_TYPE_FLOAT: {
				printf("[F %f (%llu, %llu)] ", token.fVal, token.cursor.cLine, token.cursor.cCol);
				break;
			}
			case KAT_TOKEN_TYPE_INTEGER: {
				printf("[I %d (%llu, %llu)] ", token.iVal, token.cursor.cLine, token.cursor.cCol);
				break;
			}
			case KAT_TOKEN_TYPE_LPAREN: {
				printf("( ");
				break;
			}
			case KAT_TOKEN_TYPE_RPAREN: {
				printf(") ");
				break;
			}
		}
	}


	if(pStream) IStream_Release(pStream);

	return EXIT_SUCCESS;
}