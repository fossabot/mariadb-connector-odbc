/************************************************************************************
   Copyright (C) 2013,2018 MariaDB Corporation AB
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.
   
   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.
   
   You should have received a copy of the GNU Library General Public
   License along with this library; if not see <http://www.gnu.org/licenses>
   or write to the Free Software Foundation, Inc., 
   51 Franklin St., Fifth Floor, Boston, MA 02110, USA
*************************************************************************************/

#include <ma_odbc.h>

char* SkipSpacesAndComments(char **CurPtr, size_t *Length, BOOL OverWrite)
{
  char *End= *CurPtr + *Length, *Prev= NULL;

  /* Making sure that we don't have leading whitespaces and/or comments,
  and the string begins from something meainingful */
  while (*CurPtr < End && *CurPtr != Prev)
  {
    Prev= *CurPtr;
    *CurPtr= StripLeadingComments(*CurPtr, Length, OverWrite);
    *CurPtr= ltrim(*CurPtr);
    *Length= strlen(*CurPtr);
  }

  return *CurPtr;
}


char* SkipQuotedString(char **CurPtr, char *End, char Quote, my_bool AnsiQuotes, my_bool NoBackslashEscape)
{
  while (*CurPtr < End && **CurPtr != Quote)
  {
    /* Skipping backslash and next character, if needed */
    if (**CurPtr == '\\' &&  (!NoBackslashEscape || Quote == '`' || /* Backtick works with ANSI_QUOTES */
      AnsiQuotes && Quote == '"' ))/* In indetifier quotation backslash does not escape anything */
    {
      ++*CurPtr;
      /* Taking care of the case, when backslash is at last position */
      if (*CurPtr == End)
      {
        break;
      }
    }
    ++*CurPtr;
  }

  return *CurPtr;
}


int MADB_ResetParser(MADB_Stmt *Stmt, char *OriginalQuery, SQLINTEGER OriginalLength)
{
  MADB_DeleteQuery(&Stmt->Query);

  if (OriginalQuery != NULL && OriginalLength > 0)
  {
    /* We can have here not NULL-terminated string as a source, thus we need to allocate, copy meaningful characters and
    add NULL. strndup does that for us. StmtSopy may change, p points to the allocated memory */
    Stmt->Query.allocated= Stmt->Query.RefinedText= strndup(OriginalQuery, OriginalLength);

    if (Stmt->Query.allocated == NULL)
    {
      return 1;
    }

    Stmt->Query.RefinedLength=     OriginalLength;
    Stmt->Query.BatchAllowed=      DSN_OPTION(Stmt->Connection, MADB_OPT_FLAG_MULTI_STATEMENTS) ? '\1' : '\0';
    Stmt->Query.AnsiQuotes=        MADB_SqlMode(Stmt->Connection, MADB_ANSI_QUOTES);
    Stmt->Query.NoBackslashEscape= MADB_SqlMode(Stmt->Connection, MADB_NO_BACKSLASH_ESCAPES);
  }
 
  return 0;
}


void MADB_DeleteQuery(MADB_QUERY *Query)
{
  MADB_FREE(Query->allocated);
  MADB_FREE(Query->Original);
  MADB_DeleteDynamic(&Query->Tokens);

  memset(Query, 0, sizeof(MADB_QUERY));
}

int MADB_ParseQuery(MADB_QUERY * Query)
{
  /* make sure we don't have trailing whitespace or semicolon */
  Query->RefinedLength= SqlRtrim(Query->RefinedText, (int)Query->RefinedLength);
  Query->RefinedText=  ltrim(Query->RefinedText);
  Query->RefinedText=  FixIsoFormat(Query->RefinedText, &Query->RefinedLength);
  Query->RefinedLength-= Query->RefinedText - Query->allocated;

  /* Making copy of "original" string, with minimal changes required to be able to execute */
  Query->Original= strndup(Query->RefinedText, Query->RefinedLength);
  SkipSpacesAndComments(&Query->RefinedText, &Query->RefinedLength, FALSE);

  return ParseQuery(Query);
}


char *MADB_Token(MADB_QUERY *Query, unsigned int Idx)
{
  char *p;
  unsigned int Offset= 0;
  
  p= Query->RefinedText;
  if (!Query->Tokens.elements || !p)
    return NULL;
  if (Idx >= Query->Tokens.elements)
    return NULL;

  MADB_GetDynamic(&Query->Tokens, (char *)&Offset, Idx);
  return Query->RefinedText + Offset;
}


my_bool MADB_CompareToken(MADB_QUERY *Query, unsigned int Idx, char *Compare, size_t Length, unsigned int *Offset)
{
  char *TokenString;
  
  if (!(TokenString= MADB_Token(Query, Idx)))
    return FALSE;
  if (_strnicmp(TokenString, Compare, Length) == 0)
  {
    if (Offset)
      *Offset= (unsigned int)(TokenString - Query->RefinedText);
    return TRUE;
  }
 
  return FALSE;
}

/* Not used atm, but may be useful */
unsigned int MADB_FindToken(MADB_QUERY *Query, char *Compare)
{
  unsigned int i, TokenCount= Query->Tokens.elements;
  unsigned int Offset= 0;

  for (i=0; i < TokenCount; i++)
  {
    if (MADB_CompareToken(Query, i, Compare, strlen(Compare), &Offset))
      return Offset;
  }
  return 0;
}

char *MADB_ParseCursorName(MADB_QUERY *Query, unsigned int *Offset)
{
  unsigned int i, TokenCount= Query->Tokens.elements;

  if (TokenCount < 4)
    return NULL;
  for (i=0; i < TokenCount - 3; i++)
  {
    if (MADB_CompareToken(Query, i, "WHERE", 5, Offset) &&
        MADB_CompareToken(Query, i+1, "CURRENT", 7, 0) &&
        MADB_CompareToken(Query, i+2, "OF", 2, 0))
    {
      return MADB_Token(Query, i + 3);
    }
  }
  return NULL;
}


/* Not used - rather a placeholder in case we need it */
const char * MADB_FindParamPlaceholder(MADB_Stmt *Stmt)
{ 
  return STMT_STRING(Stmt);
}


enum enum_madb_query_type MADB_GetQueryType(const char *Token1, const char *Token2)
{
  /* We need for the case when MS Access adds parenthesis around query - see ODBC-57*/
  while (*Token1 && !isalpha(*Token1))
    ++Token1;
  if (_strnicmp(Token1, "SELECT", 6) == 0)
  {
    return MADB_QUERY_SELECT;
  }
  if (_strnicmp(Token1, "INSERT", 6) == 0)
  {
    return MADB_QUERY_INSERT;
  }
  if (_strnicmp(Token1, "UPDATE", 6) == 0)
  {
    return MADB_QUERY_UPDATE;
  }
  if (_strnicmp(Token1, "DELETE", 6) == 0)
  {
    return MADB_QUERY_DELETE;
  }
  if (_strnicmp(Token1, "CALL", 4) == 0)
  {
    return MADB_QUERY_CALL;
  }
  if (_strnicmp(Token1, "SHOW", 4) == 0)
  {
    return MADB_QUERY_SHOW;
  }
  if (_strnicmp(Token1, "ANALYZE", 7) == 0)
  {
    return MADB_QUERY_ANALYZE;
  }
  if (_strnicmp(Token1, "EXPLAIN", 7) == 0)
  {
    return MADB_QUERY_EXPLAIN;
  }
  if (_strnicmp(Token1, "CHECK", 5) == 0)
  {
    return MADB_QUERY_CHECK;
  }
  if (_strnicmp(Token1, "EXECUTE", 7) == 0)
  {
    return MADB_QUERY_EXECUTE;
  }
  if (_strnicmp(Token1, "CREATE", 6) == 0)
  {
    if (_strnicmp(Token2, "PROCEDURE", 9) == 0)
    {
      return MADB_QUERY_CREATE_PROC;
    }
    if (_strnicmp(Token2, "FUNCTION", 8) == 0)
    {
      return MADB_QUERY_CREATE_FUNC;
    }
    if (_strnicmp(Token2, "DEFINER", 7) == 0)
    {
      return MADB_QUERY_CREATE_DEFINER;
    }
  }
  if (_strnicmp(Token1, "SET", 3) == 0)
  {
    if (_strnicmp(Token2, "NAMES", 5) == 0)
    {
      return MADB_QUERY_SET_NAMES;
    }
    else
    {
      return MADB_QUERY_SET;
    }
  }
  return MADB_QUERY_NO_RESULT;
}


/* Function assumes that query string has been trimmed */
char* FixIsoFormat(char * StmtString, size_t *Length)
{
  if (StmtString[0] == '{' && StmtString[*Length -1] == '}')
  {
    char *Res;

    ++StmtString;
    StmtString[*Length - 1]= '\0';

    Res= trim(StmtString);
    *Length= strlen(Res);

    return Res;
  }

  return StmtString;
}

#define SAVE_TOKEN(PTR2SAVE) do { Offset= (unsigned int)(PTR2SAVE - Query->RefinedText);\
MADB_InsertDynamic(&Query->Tokens, (char*)&Offset); } while(0)

int ParseQuery(MADB_QUERY *Query)
{
  char        *p= Query->RefinedText, Quote;
  BOOL         ReadingToken= FALSE;
  unsigned int Offset, StmtTokensCount= 0;
  size_t       Length= Query->RefinedLength;
  char        *end= p + Length;
  enum enum_madb_query_type StmtType;

  MADB_InitDynamicArray(&Query->Tokens, sizeof(unsigned int), 20, 20);

  while (p < end)
  {
    if (ReadingToken == FALSE)
    {
      Length= end - p;
      SkipSpacesAndComments(&p, &Length, TRUE);

      SAVE_TOKEN(p);
      
      ++StmtTokensCount;
      ReadingToken= TRUE;

      /* On saving 1st statement's token, we are incrementing statements counter */
      if (StmtTokensCount == 1)
      {
        ++Query->MultiStmtCount;
      }
      /* Having 2 first tockens we can get statement type. And we need to know it for the case of multistatement -
         some statements may "legally" contain ';' */
      else if (StmtTokensCount == 2)
      {
        /* We are currently at 2nd token of statement, and getting previous token position from Tokens array*/
        StmtType= MADB_GetQueryType(MADB_Token(Query, Query->Tokens.elements - 2), p);
        Query->ReturnsResult= Query->ReturnsResult || !QUERY_DOESNT_RETURN_RESULT(StmtType);
        /* If we on first statement, setting QueryType*/
        if (Query->Tokens.elements == 2)
        {
          Query->QueryType= StmtType;
        }
      }
      switch (*p)
      {
      /* If some of them is opening a string, on the fall-through next `quote` won't be set,
         as STRING_OR_COMMENT will be `true`. Likewise, if we are already in the string. But if we get hear,
         we are not supposet to be inside a string */
      case '"':
      case '\'':
      case '`':
        Quote= *p++;
        SkipQuotedString(&p, end, Quote, Query->AnsiQuotes, Query->NoBackslashEscape);
        break;
      case '?': /* This can break token(w/out space char), and be beginning of a token.
                   Thus we need it in both places */
        Query->HasParameters= 1;
        /* Parameter placeholder is a complete token. And next one may begin right after it*/
        ReadingToken= FALSE;
        break;
      case ';':
        if (QueryIsPossiblyMultistmt(Query))
        {
          /* If batches are not allowed, we only need the fact, that this is multi-statement */
          if (Query->BatchAllowed)
          {
            *p= '\0';
          }
          StmtTokensCount= 0;
        }
        ReadingToken= FALSE;
        /* We should not move pointer here */
        break;
      }
    }
    else
    {
      switch (*p)
      {
      case '?':
      case '"':
      case '\'':
      case '`':
      case ' ':
      case '\t':
      case '\r':
      case '\n':
      case '-':
      case '#':
      case '/':
      case ';':
        ReadingToken= FALSE;
        /* We should not move pointer here, since this can be already beginning of new token */
        continue;
      default:
        break;
      }
    }

    ++p;
  }

  return 0;
}


char * StripLeadingComments(char *Str, size_t *Length, BOOL OverWrite)
{
  char *Res= Str;
  int ClosingStrLen= 1;

  /* There is nothing to strip for sure */
  if (*Length == 0)
  {
    return Str;
  }

  if (strncmp(Str, "--", 1) == 0)
  {
    Res= strchr(Str + 2, '\n');
  }
  else if (*Str == '#')
  {
    Res= strchr(Str + 1, '\n');
  }
  else if (strncmp(Str, "/*", 2) == 0)
  {
    Res= strstr(Str + 2, "*/");
    ClosingStrLen= 2;
  }

  if (Res != Str)
  {
    if (Res != NULL)
    {
      Res+= ClosingStrLen;
      *Length-= Res - Str;
    }
    else /* We found comment opening string, but did not find the closing string */
    {
      /* Thus moving pointer to the end of the string */
      Res= Str + *Length;
      *Length= 0;
    }

    /* On request - overwriting comment with white spaces */
    if (OverWrite)
    {
      memset(Str, ' ', Res - Str);
    }
  }

  return Res;
}
