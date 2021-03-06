//
/// @file Library/ConfigLib/ConfigLib.c
///
/// Configuration library
///

#include <Library/ConfigLib.h>

#include <Library/LogLib.h>
#include <Library/SmBiosLib.h>

#include <Library/UefiBootServicesTableLib.h>

// GUI_CONFIG_FILE
/// Configuration file
#define CONFIG_FILE PROJECT_ROOT_PATH L"\\" PROJECT_SAFE_NAME L"\\" PROJECT_SAFE_NAME L".xml"
// GUI_ARCH_CONFIG_FILE
/// Architecture specific configuration file
#define CONFIG_ARCH_FILE PROJECT_ROOT_PATH L"\\" PROJECT_SAFE_NAME L"\\" PROJECT_SAFE_ARCH L"\\" PROJECT_SAFE_NAME L".xml"

// CONFIG_PARSE
/// Parse configuration information from XML document tree
/// @param Tree The XML document tree to parse
/// @return Whether the configuration was parsed successfully or not
/// @retval EFI_INVALID_PARAMETER If Tree is NULL
/// @retval EFI_SUCCESS           If the configuration was parsed successfully
typedef EFI_STATUS
(EFIAPI
*CONFIG_PARSE) (
  IN XML_TREE *Tree
);
// CONFIG_FREE
/// Free configuration values with a configuration path
/// @param Path The configuration path to use as root to free or NULL for root (same as ConfigFree)
/// @return Whether the configuration values were freed or not
/// @retval EFI_SUCCESS If the configuration values were freed 
typedef EFI_STATUS
(EFIAPI
*CONFIG_FREE) (
  IN CHAR16 *Path OPTIONAL
);
// CONFIG_GET_LIST
/// Get a list of names for the children of the configuration path
/// @param Path  The configuration path of which to get child names or NULL for root
/// @param List  On output, the string list of names of children
/// @param Count On output, the count of strings in the list
/// @return Whether the string list was returned or not
/// @retval EFI_INVALID_PARAMETER If List or Count is NULL or *List is not NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for the stirng list
/// @retval EFI_NOT_FOUND         If the configuration path was not found
/// @retval EFI_ACCESS_DENIED     If the configuration path is protected
/// @retval EFI_SUCCESS           If the string list was returned successfully
typedef EFI_STATUS
(EFIAPI
*CONFIG_GET_LIST) (
  IN  CHAR16   *Path OPTIONAL,
  OUT CHAR16 ***List,
  OUT UINTN    *Count
);
// CONFIG_GET_VALUE
/// Get a configuration value
/// @param Path  The path of the configuration value
/// @param Type  On output, the type of the configuration value
/// @param Value On output, the value of the configuration value
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path, Type, or Value is NULL
/// @retval EFI_NOT_FOUND         If the configuration value was not found
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
typedef EFI_STATUS
(EFIAPI
*CONFIG_GET_VALUE) (
  IN  CHAR16       *Path,
  OUT CONFIG_TYPE  *Type,
  OUT CONFIG_VALUE *Value OPTIONAL
);
// CONFIG_SET_VALUE
/// Set a configuration value
/// @param Path  The path of the configuration value
/// @param Type  The configuration type to set
/// @param Value The configuration value to set
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path or Value is NULL or Type is invalid
/// @retval EFI_SUCCESS           If the configuration value was set successfully
typedef EFI_STATUS
(EFIAPI
*CONFIG_SET_VALUE) (
  IN CHAR16       *Path,
  IN CONFIG_TYPE   Type,
  IN CONFIG_VALUE *Value
);

// CONFIG_PROTOCOL
/// Configuration tree protcol
typedef struct _CONFIG_PROTOCOL CONFIG_PROTOCOL;
struct _CONFIG_PROTOCOL {

  // LoadFromString
  /// Load configuration information from string
  CONFIG_PARSE      Parse;
  // Free
  /// Free configuration values with a configuration path
  CONFIG_FREE       Free;
  // GetList
  /// Get a list of names for the children of the configuration path
  CONFIG_GET_LIST   GetList;
  // GetValue
  /// Get a configuration value
  CONFIG_GET_VALUE  GetValue;
  // SetValue
  /// Set a configuration value
  CONFIG_SET_VALUE  SetValue;

};

// CONFIG_TREE
/// Configuration tree node
typedef struct _CONFIG_TREE CONFIG_TREE;
struct _CONFIG_TREE {

  // Next
  /// The next node at this level
  CONFIG_TREE  *Next;
  // Children
  /// The nodes that are children of this node
  CONFIG_TREE  *Children;
  // Name
  /// The name of this node
  CHAR16       *Name;
  // Type
  /// The type of this node
  CONFIG_TYPE   Type;
  // Value
  /// The value of this node
  CONFIG_VALUE  Value;

};

// CONFIG_INSPECT_AUTO_GROUP
/// This configuration key must always be grouped, any children will be placed inside of group zero if not grouped
#define CONFIG_INSPECT_AUTO_GROUP 0x1

// CONFIG_INSPECT
/// Configuration XML inspection callback data
typedef struct _CONFIG_INSPECT CONFIG_INSPECT;
struct _CONFIG_INSPECT {

  // Path
  /// The path to the configuration value
  CHAR16  *Path;
  // Options
  /// The options for the configuration value
  UINTN    Options;

};

// mConfigGuid
/// The configuration protocol GUID
STATIC EFI_GUID         mConfigGuid = { 0x2F4BD4A0, 0x227B, 0x4967, { 0x8B, 0xB0, 0xE6, 0xB7, 0xD5, 0xF9, 0x8F, 0x16 } };
// mConfigHandle
/// The configuration protocol handle
STATIC EFI_HANDLE       mConfigHandle = NULL;
// mConfig
/// The installed configuration protocol
STATIC CONFIG_PROTOCOL *mConfig = NULL;
// mConfigTree
/// The configuration tree root node
STATIC CONFIG_TREE     *mConfigTree = NULL;
// mConfigAutoGroups
/// The configuration auto group keys
STATIC CHAR16          *mConfigAutoGroups[] = {
  L"\\CPU\\Package",
  L"\\Memory\\Slot"
};

// ConfigFind
/// Find a configuration tree node by path
/// @param Path The path of the configuration tree node
/// @param Tree On output, the address of the configuration tree node
/// @return Whether the 
STATIC EFI_STATUS
EFIAPI
ConfigFind2 (
  IN  CHAR16        *Path OPTIONAL,
  IN  BOOLEAN        Create,
  OUT CONFIG_TREE ***Tree
) {
  CONFIG_TREE **Node;
  CHAR16       *Next;
  CHAR16       *Name;
  // Check parameters
  if (Tree == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  // Check if there are any nodes
  if (mConfigTree == NULL) {
    if (Create) {
      // Create new root node if needed
      mConfigTree = (CONFIG_TREE *)AllocateZeroPool(sizeof(CONFIG_TREE));
      if (mConfigTree == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }
      mConfigTree->Next = NULL;
      mConfigTree->Children = NULL;
      mConfigTree->Name = NULL;
      mConfigTree->Type = CONFIG_TYPE_LIST;
    } else {
      // Not found
      return EFI_NOT_FOUND;
    }
  }
  // Check if searching for root could be NULL, "", or "\"
  Node = &mConfigTree;
  if (Path != NULL) {
    if (*Path == L'\\') {
      ++Path;
    }
    // Iterate through the configuration tree nodes
    while (*Path != L'\0') {
      // Check if there are any children
      if (!Create && ((*Node)->Children == NULL)) {
        if (StriCmp(Path, (*Node)->Name) == 0) {
          break;
        }
        // Not found
        return EFI_NOT_FOUND;
      }
      // Get the next node start
      Next = StrStr(Path, L"\\");
      if (Next == NULL) {
        // Get the node name for which to search
        Name = StrDup(Path);
        // Set to end of path if not found
        Path += StrLen(Path);
      } else if (Next == Path) {
        // Skip consecutive separators
        ++Path;
        continue;
      } else {
        // Get the node name for which to search
        Name = StrnDup(Path, Next - Path);
        // Set to after the separator
        Path = Next + 1;
      }
      if (Name == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }
      // Iterate through children for name
      for (Node = &((*Node)->Children); *Node != NULL; Node = &((*Node)->Next)) {
        // Try to match child name
        if (((*Node)->Name != NULL) && (StriCmp(Name, (*Node)->Name) == 0)) {
          break;
        }
      }
      // Check if found
      if (*Node == NULL) {
        if (Create) {
          // Create new node if needed
          *Node = (CONFIG_TREE *)AllocateZeroPool(sizeof(CONFIG_TREE));
          if (*Node == NULL) {
            FreePool(Name);
            return EFI_OUT_OF_RESOURCES;
          }
          (*Node)->Next = NULL;
          (*Node)->Children = NULL;
          (*Node)->Type = CONFIG_TYPE_LIST;
          (*Node)->Name = Name;
        } else {
          // Not found
          FreePool(Name);
          return EFI_NOT_FOUND;
        }
      } else {
        FreePool(Name);
      }
    }
  }
  // Check if found
  if (*Node == NULL) {
    return EFI_NOT_FOUND;
  }
  // Return the tree node
  *Tree = Node;
  return EFI_SUCCESS;
}
// ConfigFind
/// Find a configuration tree node by path
/// @param Path The path of the configuration tree node
/// @return The configuration tree node or NULL if not found
STATIC EFI_STATUS
EFIAPI
ConfigFind (
  IN  CHAR16       *Path OPTIONAL,
  IN  BOOLEAN       Create,
  OUT CONFIG_TREE **Tree
) {
  CONFIG_TREE **Node = NULL;
  EFI_STATUS    Status;
  Status = ConfigFind2(Path, Create, &Node);
  if (!EFI_ERROR(Status) && (Node != NULL)) {
    *Tree = *Node;
  }
  return Status;
}
// ConfigTreeFree
/// Free configuration tree node
/// @param Tree The configuration tree node
/// @return Whether the configuration tree node was freed or not
/// @retval EFI_INVALID_PARAMETER If Tree is NULL
/// @retval EFI_SUCCESS           If the node was freed successfully
STATIC EFI_STATUS
EFIAPI
ConfigTreeFree (
  IN CONFIG_TREE *Tree
) {
  // Check parameters
  if (Tree == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (Tree->Name != NULL) {
    FreePool(Tree->Name);
    Tree->Name = NULL;
  }
  // Free value
  switch (Tree->Type) {
  case CONFIG_TYPE_STRING:
    // Free string value
    if (Tree->Value.String != NULL) {
      FreePool(Tree->Value.String);
      Tree->Value.String = NULL;
    }
    break;

  case CONFIG_TYPE_DATA:
    // Free data value
    if (Tree->Value.Data.Data != NULL) {
      FreePool(Tree->Value.Data.Data);
    }

  default:
    // Set every thing else to empty
    Tree->Value.Data.Size = 0;
    Tree->Value.Data.Data = NULL;
    break;
  }
  // Free any children
  if (Tree->Children != NULL) {
    CONFIG_TREE *Child = Tree->Children;
    while (Child != NULL) {
      CONFIG_TREE *Tmp = Child;
      Child = Tmp->Next;
      ConfigTreeFree(Tmp);
    }
    Tree->Children = NULL;
  }
  // Free node
  FreePool(Tree);
  return EFI_SUCCESS;
}

// ConfigLoad
/// Load configuration information from file
/// @param Root If Path is NULL the file handle to use to load, otherwise the root file handle
/// @param Path If Root is NULL the full device path string to the file, otherwise the root relative path
/// @return Whether the configuration was loaded successfully or not
/// @retval EFI_INVALID_PARAMETER If Root and Path are both NULL
/// @retval EFI_NOT_FOUND         If the configuration file could not be opened
/// @retval EFI_SUCCESS           If the configuration file was loaded successfully
EFI_STATUS
EFIAPI
ConfigLoad (
  IN EFI_FILE_HANDLE  Root OPTIONAL,
  IN CHAR16          *Path OPTIONAL
) {
  EFI_STATUS      Status;
  EFI_FILE_HANDLE Handle = NULL;
  // Check parameters
  if ((Root == NULL) && (Path == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  if (Path != NULL) {
    // Open configuration file handle
    Log2(L"Configuration:", L"\"%s\"\n", Path);
    Status = FileHandleOpen(&Handle, Root, Path, EFI_FILE_MODE_READ, 0);
  } else {
    // Get file name of file handle
    Status = FileHandleGetFileName(Root, &Path);
    if (!EFI_ERROR(Status) && (Path != NULL)) {
      // Open configuration file handle
      Log2(L"Configuration:", L"\"%s\"\n", Path);
      Status = FileHandleOpen(&Handle, Root, Path, EFI_FILE_MODE_READ, 0);
      FreePool(Path);
    }
  }
  // If file handle is open parse configuration
  if (!EFI_ERROR(Status) && (Handle != NULL)) {
    UINT64 FileSize = 0;
    UINTN  Size = 0;
    // Get file size to read the configuration
    Status = FileHandleGetSize(Handle, &FileSize);
    if (!EFI_ERROR(Status)) {
      if (FileSize == 0) {
        // Assume not found if no file size
        Status = EFI_NOT_FOUND;
      } else {
        // Allocate buffer to hold configuration string
        VOID *Config = (VOID *)AllocateZeroPool(Size = (UINTN)FileSize);
        if (Config == NULL) {
          Status = EFI_OUT_OF_RESOURCES;
        } else {
          // Read configuration string from file
          Status = FileHandleRead(Handle, &Size, (VOID *)Config);
          if (!EFI_ERROR(Status)) {
            Status = ConfigParse(Size, Config);
          }
          FreePool(Config);
        }
      }
    }
    // Close the file handle
    FileHandleClose(Handle);
  }
  Log2(L"  Load status:", L"%r\n", Status);
  return Status;
}
// ConfigParse
/// Parse configuration information from string
/// @param Size   The size, in bytes, of the configuration string
/// @param Config The configuration string to parse
/// @return Whether the configuration was parsed successfully or not
/// @retval EFI_INVALID_PARAMETER If Config is NULL or Size is zero
/// @retval EFI_SUCCESS           If the configuration string was parsed successfully
EFI_STATUS
EFIAPI
ConfigParse (
  IN UINTN  Size,
  IN VOID  *Config
) {
  EFI_STATUS  Status;
  XML_PARSER *Parser = NULL;
  // Check parameters
  if ((Config == NULL) || (Size == 0)) {
    return EFI_INVALID_PARAMETER;
  }
  // Create XML parser
  Status = XmlCreate(&Parser);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  if (Parser == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Parse the XML buffer
  Status = XmlParse(Parser, Size, Config);
  if (!EFI_ERROR(Status)) {
    XML_TREE *Tree = NULL;
    // Get the XML document tree root node
    Status = XmlGetTree(Parser, &Tree);
    if (!EFI_ERROR(Status)) {
      // Parse the configuration
      Status = ConfigParseXml(Tree);
    }
  }
  // Free the XML parser
  XmlFree(Parser);
  return Status;
}

// ConfigXmlInspector
/// Configuration XML document tree inspection callback
/// @param Tree           The document tree node
/// @param Level          The level of generation of tree nodes, zero for the root
/// @param LevelIndex     The index of the tree node relative to the previous level
/// @param TagName        The tree node tag name
/// @param Value          The tree node value
/// @param AttributeCount The tree node attribute count
/// @param Attributes     The tree node attributes
/// @param ChildCount     The tree node child count
/// @param Children       The tree node children
/// @param Context        The context passed when inspection started
/// @retval TRUE  If the inspection should continue
/// @retval FALSE If the inspection should stop
STATIC BOOLEAN
EFIAPI
ConfigXmlInspector (
  IN XML_TREE       *Tree,
  IN UINTN           Level,
  IN UINTN           LevelIndex,
  IN CHAR16         *TagName,
  IN CHAR16         *Value OPTIONAL,
  IN UINTN           AttributeCount,
  IN XML_ATTRIBUTE **Attributes OPTIONAL,
  IN UINTN           ChildCount,
  IN XML_TREE      **Children OPTIONAL,
  IN VOID           *Context OPTIONAL
) {
  CONFIG_INSPECT *Parent = (CONFIG_INSPECT *)Context;
  CONFIG_INSPECT  This = { 0, 0 };
  UINTN           Index;
  // Check parameters
  if ((Tree == NULL) || (TagName == NULL)) {
    return TRUE;
  }
  // Get attributes
  if ((Attributes != NULL) && (AttributeCount > 0)) {
    // Iterate through attributes
    for (Index = 0; Index < AttributeCount; ++Index) {
      if ((Attributes[Index] != NULL) && (Attributes[Index]->Name != NULL)) {
        if (StriCmp(Attributes[Index]->Name, L"arch") == 0) {
          // Check architectures match
          if ((Attributes[Index]->Value == NULL) || (StriCmp(Attributes[Index]->Value, PROJECT_ARCH) != 0)) {
            // Skip this tree node since it's intended for a different architecture
            return TRUE;
          }
        } else if (StriCmp(Attributes[Index]->Name, L"manufacturer") == 0) {
          // Check manufacturer matches
          CHAR16 *NewManufacturer;
          CHAR8  *Manufacturer = GetSmBiosManufacturer();
          UINTN   Length = AsciiStrLen(Manufacturer) + 1;
          NewManufacturer = (CHAR16 *)AllocateZeroPool(Length * sizeof(CHAR16));
          if (NewManufacturer == NULL) {
            return TRUE;
          }
          AsciiStrToUnicodeStrS(Manufacturer, NewManufacturer, Length);
          if ((Attributes[Index]->Value == NULL) || (StriStr(NewManufacturer, Attributes[Index]->Value) != NULL)) {
            // Skip this tree node since it's intended for a different manufacturer
            FreePool(NewManufacturer);
            return TRUE;
          }
          FreePool(NewManufacturer);
        } else if (StriCmp(Attributes[Index]->Name, L"product") == 0) {
          // Check product matches
          CHAR16 *NewProductName;
          CHAR8  *ProductName = GetSmBiosProductName();
          UINTN   Length = AsciiStrLen(ProductName) + 1;
          NewProductName = (CHAR16 *)AllocateZeroPool(Length * sizeof(CHAR16));
          if (NewProductName == NULL) {
            return TRUE;
          }
          AsciiStrToUnicodeStrS(ProductName, NewProductName, Length);
          if ((Attributes[Index]->Value == NULL) || (StriStr(NewProductName, Attributes[Index]->Value) != NULL)) {
            // Skip this tree node since it's intended for a different ProductName
            FreePool(NewProductName);
            return TRUE;
          }
          FreePool(NewProductName);
        }
      }
    }
  }
  // Check for group type
  if (StriCmp(TagName, L"group") == 0) {
    // Create the index of this group
    CHAR16 *IndexPath = CatSPrint(NULL, L"%u", LevelIndex);
    if (IndexPath != NULL) {
      This.Path = FileMakePath((Parent == NULL) ? NULL : Parent->Path, IndexPath);
      FreePool(IndexPath);
    }
  } else if ((Level == 1) && (ChildCount == 0) && (Value != NULL) && (StriCmp(TagName, L"include") == 0)) {
    // Include another configuration
    ConfigLoad(NULL, Value);
    return TRUE;
  } else if ((Parent != NULL) && ((Parent->Options & CONFIG_INSPECT_AUTO_GROUP) != 0)) {
    // Auto group this partial path
    CHAR16 *IndexPath = CatSPrint(NULL, L"%u\\%s", 0, TagName);
    if (IndexPath != NULL) {
      This.Path = FileMakePath((Parent == NULL) ? NULL : Parent->Path, IndexPath);
      FreePool(IndexPath);
    }
  } else {
    // Create full path
    This.Path = FileMakePath((Parent == NULL) ? NULL : Parent->Path, TagName);
  }
  if (This.Path == NULL) {
    return TRUE;
  }
  // Get children
  if ((Children != NULL) && (ChildCount > 0)) {
    // Check for some built in types
    if ((ChildCount == 1) && !XmlTreeHasChildren(Children[0])) {
      CHAR16 *Name = NULL;
      if (!EFI_ERROR(XmlTreeGetTag(Children[0], &Name)) && (Name != NULL)) {
        // Check which type
        if (StriCmp(Name, L"integer") == 0) {
          // Integer value
          Name = NULL;
          if (!EFI_ERROR(XmlTreeGetValue(Children[0], &Name)) && (Name != NULL)) {
            INTN Value = 1;
            if (*Name == L'-') {
              Value = -1;
              ++Name;
            }
            if ((*Name == L'0') && ((Name[1] == L'x') || (Name[1] == L'X'))) {
              Value *= (INTN)StrHexToUintn(Name + 2);
              LOG(L"  %s=0x%0*X\n", This.Path, sizeof(UINTN) << 1, Value);
            } else {
              Value *= (INTN)StrDecimalToUintn(Name);
              LOG(L"  %s=%d\n", This.Path, Value);
            }
            ConfigSetInteger(This.Path, Value);
          }
          FreePool(This.Path);
          return TRUE;
        } else if (StriCmp(Name, L"unsigned") == 0) {
          // Unsigned integer value
          Name = NULL;
          if (!EFI_ERROR(XmlTreeGetValue(Children[0], &Name)) && (Name != NULL)) {
            UINTN Value;
            if ((*Name == L'0') && ((Name[1] == L'x') || (Name[1] == L'X'))) {
              Value = StrHexToUintn(Name + 2);
              LOG(L"  %s=0x%0*X\n", This.Path, sizeof(UINTN) << 1, Value);
            } else {
              Value = StrDecimalToUintn(Name);
              LOG(L"  %s=%u\n", This.Path, Value);
            }
            ConfigSetUnsigned(This.Path, Value);
          }
          FreePool(This.Path);
          return TRUE;
        } else if (StriCmp(Name, L"data") == 0) {
          // Data base64 value
          UINTN  Size = 0;
          VOID  *Data = NULL;
          if (!EFI_ERROR(XmlTreeGetValue(Children[0], &Name)) && (Name != NULL)) {
            if (!EFI_ERROR(FromBase64(Name, &Size, &Data)) && (Data != NULL)) {
              if (Size > 0) {
                LOG(L"  %s=%s\n", This.Path, Name);
                ConfigSetData(This.Path, Size, Data);
              }
              FreePool(Data);
            }
          }
          FreePool(This.Path);
          return TRUE;
        } else if (StriCmp(Name, L"boolean") == 0) {
          // Boolean value
          Name = NULL;
          if (!EFI_ERROR(XmlTreeGetValue(Children[0], &Name)) && (Name != NULL)) {
            BOOLEAN Value = ((*Name == L't') || (*Name == L'T') ||
                             ((*Name == L'0') && ((Name[1] == L'x') || (Name[1] == L'X')) && (StrHexToUintn(Name + 2) != 0)) ||
                             (StrDecimalToUintn(Name) != 0));
            LOG(L"  %s=%s\n", This.Path, Value ? L"true" : L"false");
            ConfigSetBoolean(This.Path, Value);
          }
          FreePool(This.Path);
          return TRUE;
        } else if (StriCmp(Name, L"true") == 0) {
          // True
          LOG(L"  %s=true\n", This.Path);
          ConfigSetBoolean(This.Path, TRUE);
          FreePool(This.Path);
          return TRUE;
        } else if (StriCmp(Name, L"false") == 0) {
          // False
          LOG(L"  %s=false\n", This.Path);
          ConfigSetBoolean(This.Path, FALSE);
          FreePool(This.Path);
          return TRUE;
        }
      }
    }
    // Check if this key is auto grouped
    for (Index = 0; Index < ARRAY_SIZE(mConfigAutoGroups); ++Index) {
      if (StriCmp(This.Path, mConfigAutoGroups[Index]) == 0) {
        This.Options = CONFIG_INSPECT_AUTO_GROUP;
        break;
      }
    }
    // Iterate through children
    for (Index = 0; Index < ChildCount; ++Index) {
      // Inspect each child
      XmlTreeInspect(Children[Index], Level + 1, Index, ConfigXmlInspector, (VOID *)&This, FALSE);
    }
  } else if (Value != NULL) {
    // Value
    LOG(L"  %s=\"%s\"\n", This.Path, Value);
    ConfigSetString(This.Path, Value);
  }
  FreePool(This.Path);
  return TRUE;
}
// ConfigParseXml
/// Parse configuration information from XML document tree
/// @param Tree The XML document tree to parse
/// @return Whether the configuration was parsed successfully or not
/// @retval EFI_INVALID_PARAMETER If Tree is NULL
/// @retval EFI_SUCCESS           If the configuration was parsed successfully
EFI_STATUS
EFIAPI
ConfigParseXml (
  IN XML_TREE *Tree
) {
  XML_TREE **Children = NULL;
  CHAR16    *Name = NULL;
  UINTN      Count = 0;
  UINTN      Index;
  // Check parameters
  if (Tree == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  // Use configuration protocol if present
  if ((mConfig != NULL) && (mConfig->Parse != NULL)) {
    return mConfig->Parse(Tree);
  }
  if (EFI_ERROR(XmlTreeGetTag(Tree, &Name)) || (Name == NULL) || (StriCmp(Name, L"configuration") != 0)) {
    return EFI_INVALID_PARAMETER;
  }
  // Inspect the XML tree
  if (!EFI_ERROR(XmlTreeGetChildren(Tree, &Children, &Count)) && (Children != NULL) && (Count > 0)) {
    for (Index = 0; Index < Count; ++Index) {
      EFI_STATUS Status = XmlTreeInspect(Children[Index], 1, Index, ConfigXmlInspector, NULL, FALSE);
      if (EFI_ERROR(Status)) {
        FreePool(Children);
        return Status;
      }
    }
  }
  if (Children != NULL) {
    FreePool(Children);
  }
  return EFI_SUCCESS;
}

// ConfigFree
/// Free all configuration values
/// @return Whether the configuration values were freed or not
/// @retval EFI_SUCCESS If the configuration values were freed
EFI_STATUS
EFIAPI
ConfigFree (
  VOID
) {
  return ConfigPartialFree(NULL);
}
// ConfigPartialFree
/// Free configuration values with a configuration path
/// @param Path The configuration path to use as root to free or NULL for root (same as ConfigFree)
/// @return Whether the configuration values were freed or not
/// @retval EFI_SUCCESS If the configuration values were freed
EFI_STATUS
EFIAPI
ConfigPartialFree (
  IN CHAR16 *Path OPTIONAL
) {
  CONFIG_TREE **Tree = NULL;
  CONFIG_TREE  *Node;
  EFI_STATUS    Status;
  // Use configuration protocol if present
  if ((mConfig != NULL) && (mConfig->Free != NULL)) {
    return mConfig->Free(Path);
  }
  // Find the address of the configuration tree node
  Status = ConfigFind2(Path, FALSE, &Tree);
  if (EFI_ERROR(Status) || (Tree == NULL) || (*Tree == NULL)) {
    return (Status == EFI_NOT_FOUND) ? EFI_SUCCESS : Status;
  }
  // Replace the node with the next
  Node = *Tree;
  *Tree = Node->Next;
  // Free the node
  return ConfigTreeFree(Node);
}
// ConfigSPartialFree
/// Free configuration values with a configuration path
/// @param Path The configuration path to use as root to free
/// @param ...  The argument list
/// @return Whether the configuration values were freed or not
/// @retval EFI_INVALID_PARAMETER If Path is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated
/// @retval EFI_SUCCESS           If the configuration values were freed
EFI_STATUS
EFIAPI
ConfigSPartialFree (
  IN CHAR16 *Path,
  ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Path);
  Status = ConfigVSPartialFree(Path, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSPartialFree
/// Free configuration values with a configuration path
/// @param Path The configuration path to use as root to free
/// @param Args The argument list
/// @return Whether the configuration values were freed or not
/// @retval EFI_INVALID_PARAMETER If Path is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated
/// @retval EFI_SUCCESS           If the configuration values were freed
EFI_STATUS
EFIAPI
ConfigVSPartialFree (
  IN CHAR16  *Path,
  IN VA_LIST  Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if (Path == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Free the partial path
  Status = ConfigPartialFree(FullPath);
  FreePool(FullPath);
  return Status;
}

// ConfigGetList
/// Get a list of names for the children of the configuration path
/// @param Path  The configuration path of which to get child names or NULL for root
/// @param List  On output, the string list of names of children
/// @param Count On output, the count of strings in the list
/// @return Whether the string list was returned or not
/// @retval EFI_INVALID_PARAMETER If List or Count is NULL or *List is not NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for the stirng list
/// @retval EFI_NOT_FOUND         If the configuration path was not found
/// @retval EFI_SUCCESS           If the string list was returned successfully
EFI_STATUS
EFIAPI
ConfigGetList (
  IN  CHAR16   *Path OPTIONAL,
  OUT CHAR16 ***List,
  OUT UINTN    *Count
) {
  EFI_STATUS   Status;
  CONFIG_TREE *Node;
  // Check parameters
  if ((List == NULL) || (*List != NULL) || (Count == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Use configuration protocol if present
  if ((mConfig != NULL) && (mConfig->GetList != NULL)) {
    return mConfig->GetList(Path, List, Count);
  }
  // Get the configuration tree node
  Status = ConfigFind(Path, FALSE, &Node);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  if ((Node == NULL) || (Node->Type != CONFIG_TYPE_LIST)) {
    return EFI_NOT_FOUND;
  }
  // Add the child node names to the list
  for (Node = Node->Children; Node != NULL; Node = Node->Next) {
    if (Node->Name != NULL) {
      StrList(List, Count, Node->Name, 0, STR_LIST_NO_DUPLICATES | STR_LIST_CASE_INSENSITIVE | STR_LIST_SORTED);
    }
  }
  if ((*Count == 0) || (*List == NULL)) {
    return EFI_NOT_FOUND;
  }
  return EFI_SUCCESS;
}
// ConfigSGetList
/// Get a list of names for the children of the configuration path
/// @param Path  The configuration path of which to get child names or NULL for root
/// @param List  On output, the string list of names of children
/// @param Count On output, the count of strings in the list
/// @param ...   The argument list
/// @return Whether the string list was returned or not
/// @retval EFI_INVALID_PARAMETER If Path, List, or Count is NULL or *List is not NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for the stirng list
/// @retval EFI_NOT_FOUND         If the configuration path was not found
/// @retval EFI_ACCESS_DENIED     If the configuration path is protected
/// @retval EFI_SUCCESS           If the string list was returned successfully
EFI_STATUS
EFIAPI
ConfigSGetList (
  IN  CHAR16   *Path,
  OUT CHAR16 ***List,
  OUT UINTN    *Count,
  IN  ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Count);
  Status = ConfigVSGetList(Path, List, Count, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSGetList
/// Get a list of names for the children of the configuration path
/// @param Path  The configuration path of which to get child names
/// @param List  On output, the string list of names of children
/// @param Count On output, the count of strings in the list
/// @param Args  The argument list
/// @return Whether the string list was returned or not
/// @retval EFI_INVALID_PARAMETER If Path, List, or Count is NULL or *List is not NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for the stirng list
/// @retval EFI_NOT_FOUND         If the configuration path was not found
/// @retval EFI_ACCESS_DENIED     If the configuration path is protected
/// @retval EFI_SUCCESS           If the string list was returned successfully
EFI_STATUS
EFIAPI
ConfigVSGetList (
  IN  CHAR16    *Path,
  OUT CHAR16  ***List,
  OUT UINTN     *Count,
  IN  VA_LIST    Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if ((Path == NULL) || (List == NULL) || (*List != NULL) || (Count == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Get the list
  Status = ConfigGetList(FullPath, List, Count);
  FreePool(FullPath);
  return Status;
}

// ConfigGetType
/// Get the type of a configuration value
/// @param Path The path of the configuration value of which to get the type
/// @param Type On output, the configuration value type
/// @retval EFI_INVALID_PARAMETER If Path or Type is NULL
/// @retval EFI_NOT_FOUND         If the configuration value was not found
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigGetType (
  IN  CHAR16      *Path,
  OUT CONFIG_TYPE *Type
) {
  EFI_STATUS   Status;
  CONFIG_TREE *Node;
  // Check parameters
  if ((Path == NULL) || (Type == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Get configuration tree node
  Status = ConfigFind(Path, FALSE, &Node);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  if (Node == NULL) {
    return EFI_NOT_FOUND;
  }
  // Return type
  *Type = Node->Type;
  return EFI_SUCCESS;
}
// ConfigSGetType
/// Get the type of a configuration value
/// @param Path The path of the configuration value of which to get the type
/// @param Type On output, the configuration value type
/// @param ...  The argument list
/// @retval EFI_INVALID_PARAMETER If Path or Type is NULL
/// @retval EFI_NOT_FOUND         If the configuration value was not found
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigSGetType (
  IN  CHAR16      *Path,
  OUT CONFIG_TYPE *Type,
  IN  ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Type);
  Status = ConfigVSGetType(Path, Type, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSGetType
/// Get the type of a configuration value
/// @param Path The path of the configuration value of which to get the type
/// @param Type On output, the configuration value type
/// @param Args The argument list
/// @retval EFI_INVALID_PARAMETER If Path or Type is NULL
/// @retval EFI_NOT_FOUND         If the configuration value was not found
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigVSGetType (
  IN  CHAR16      *Path,
  OUT CONFIG_TYPE *Type,
  IN  VA_LIST      Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if ((Path == NULL) || (Type == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Get the list
  Status = ConfigGetType(FullPath, Type);
  FreePool(FullPath);
  return Status;
}

// ConfigGetValue
/// Get a configuration value
/// @param Path  The path of the configuration value
/// @param Type  On output, the type of the configuration value
/// @param Value On output, the value of the configuration value
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path, Type, or Value is NULL
/// @retval EFI_NOT_FOUND         If the configuration value was not found
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigGetValue (
  IN  CHAR16       *Path,
  OUT CONFIG_TYPE  *Type,
  OUT CONFIG_VALUE *Value
) {
  EFI_STATUS   Status;
  CONFIG_TREE *Node;
  // Check parameters
  if ((Path == NULL) || (Type == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Use configuration protocol if present
  if ((mConfig != NULL) && (mConfig->GetValue != NULL)) {
    return mConfig->GetValue(Path, Type, Value);
  }
  // Get configuration tree node
  Status = ConfigFind(Path, FALSE, &Node);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  if (Node == NULL) {
    return EFI_NOT_FOUND;
  }
  // Return type and value
  *Type = Node->Type;
  CopyMem(Value, &(Node->Value), sizeof(CONFIG_VALUE));
  return EFI_SUCCESS;
}
// ConfigSGetValue
/// Get a configuration value
/// @param Path  The path of the configuration value
/// @param Type  On output, the type of the configuration value
/// @param Value On output, the value of the configuration value
/// @param ...   The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path, Type, or Value is NULL
/// @retval EFI_NOT_FOUND         If the configuration value was not found
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigSGetValue (
  IN  CHAR16       *Path,
  OUT CONFIG_TYPE  *Type,
  OUT CONFIG_VALUE *Value,
  IN  ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Type);
  Status = ConfigVSGetValue(Path, Type, Value, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSGetValue
/// Get a configuration value
/// @param Path  The path of the configuration value
/// @param Type  On output, the type of the configuration value
/// @param Value On output, the value of the configuration value
/// @param Args  The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path, Type, or Value is NULL
/// @retval EFI_NOT_FOUND         If the configuration value was not found
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigVSGetValue (
  IN  CHAR16       *Path,
  OUT CONFIG_TYPE  *Type,
  OUT CONFIG_VALUE *Value,
  IN  VA_LIST       Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if ((Path == NULL) || (Type == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Get the value
  Status = ConfigGetValue(FullPath, Type, Value);
  FreePool(FullPath);
  return Status;
}

// ConfigGetBoolean
/// Get a boolean configuration value
/// @param Path    The path of the configuration value
/// @param Boolean On output, the boolean configuration value
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path or Boolean is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigGetBoolean (
  IN  CHAR16  *Path,
  OUT BOOLEAN *Boolean
) {
  EFI_STATUS   Status;
  CONFIG_VALUE Value;
  CONFIG_TYPE  Type = CONFIG_TYPE_UNKNOWN;
  // Check parameters
  if ((Path == NULL) || (Boolean == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Get the configuration value
  Status = ConfigGetValue(Path, &Type, &Value);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  // Check type matches
  if (Type != CONFIG_TYPE_BOOLEAN) {
    return EFI_ABORTED;
  }
  *Boolean = Value.Boolean;
  return EFI_SUCCESS;
}
// ConfigSGetBoolean
/// Get a boolean configuration value
/// @param Path    The path of the configuration value
/// @param Boolean On output, the boolean configuration value
/// @param ...     The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path or Boolean is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigSGetBoolean (
  IN  CHAR16  *Path,
  OUT BOOLEAN *Boolean,
  IN  ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Boolean);
  Status = ConfigVSGetBoolean(Path, Boolean, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSGetBoolean
/// Get a boolean configuration value
/// @param Path    The path of the configuration value
/// @param Boolean On output, the boolean configuration value
/// @param Args    The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path or Boolean is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigVSGetBoolean (
  IN  CHAR16  *Path,
  OUT BOOLEAN *Boolean,
  IN  VA_LIST  Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if ((Path == NULL) || (Boolean == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Get the value
  Status = ConfigGetBoolean(FullPath, Boolean);
  FreePool(FullPath);
  return Status;
}

// ConfigGetInteger
/// Get an integer configuration value
/// @param Path    The path of the configuration value
/// @param Integer On output, the integer configuration value
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path or Integer is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigGetInteger (
  IN  CHAR16 *Path,
  OUT INTN   *Integer
) {
  EFI_STATUS   Status;
  CONFIG_VALUE Value;
  CONFIG_TYPE  Type = CONFIG_TYPE_UNKNOWN;
  // Check parameters
  if ((Path == NULL) || (Integer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Get the configuration value
  Status = ConfigGetValue(Path, &Type, &Value);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  // Check type matches
  if (Type != CONFIG_TYPE_INTEGER) {
    return EFI_ABORTED;
  }
  *Integer = Value.Integer;
  return EFI_SUCCESS;
}
// ConfigSGetInteger
/// Get an integer configuration value
/// @param Path    The path of the configuration value
/// @param Integer On output, the integer configuration value
/// @param ...     The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path or Integer is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigSGetInteger (
  IN  CHAR16 *Path,
  OUT INTN   *Integer,
  IN  ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Integer);
  Status = ConfigVSGetInteger(Path, Integer, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSGetInteger
/// Get an integer configuration value
/// @param Path    The path of the configuration value
/// @param Integer On output, the integer configuration value
/// @param Args    The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path or Integer is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigVSGetInteger (
  IN  CHAR16  *Path,
  OUT INTN    *Integer,
  IN  VA_LIST  Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if ((Path == NULL) || (Integer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Get the value
  Status = ConfigGetInteger(FullPath, Integer);
  FreePool(FullPath);
  return Status;
}

// ConfigGetUnsigned
/// Get an unsigned integer configuration value
/// @param Path    The path of the configuration value
/// @param Unsigned On output, the unsigned integer configuration value
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path or Unsigned is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigGetUnsigned (
  IN  CHAR16 *Path,
  OUT UINTN  *Unsigned
) {
  EFI_STATUS   Status;
  CONFIG_VALUE Value;
  CONFIG_TYPE  Type = CONFIG_TYPE_UNKNOWN;
  // Check parameters
  if ((Path == NULL) || (Unsigned == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Get the configuration value
  Status = ConfigGetValue(Path, &Type, &Value);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  // Check type matches
  if (Type != CONFIG_TYPE_UNSIGNED) {
    return EFI_ABORTED;
  }
  *Unsigned = Value.Unsigned;
  return EFI_SUCCESS;
}
// ConfigSGetUnsigned
/// Get an unsigned integer configuration value
/// @param Path    The path of the configuration value
/// @param Unsigned On output, the unsigned integer configuration value
/// @param ...      The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path or Unsigned is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigSGetUnsigned (
  IN  CHAR16 *Path,
  OUT UINTN  *Unsigned,
  IN  ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Unsigned);
  Status = ConfigVSGetUnsigned(Path, Unsigned, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSGetUnsigned
/// Get an unsigned integer configuration value
/// @param Path     The path of the configuration value
/// @param Unsigned On output, the unsigned integer configuration value
/// @param Args     The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path or Unsigned is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigVSGetUnsigned (
  IN  CHAR16  *Path,
  OUT UINTN   *Unsigned,
  IN  VA_LIST  Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if ((Path == NULL) || (Unsigned == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Get the value
  Status = ConfigGetInteger(FullPath, Unsigned);
  FreePool(FullPath);
  return Status;
}

// ConfigGetString
/// Get a string configuration value
/// @param Path   The path of the configuration value
/// @param String On output, the string configuration value
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path or String is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigGetString (
  IN  CHAR16  *Path,
  OUT CHAR16 **String
) {
  EFI_STATUS   Status;
  CONFIG_VALUE Value;
  CONFIG_TYPE  Type = CONFIG_TYPE_UNKNOWN;
  // Check parameters
  if ((Path == NULL) || (String == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Get the configuration value
  Status = ConfigGetValue(Path, &Type, &Value);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  // Check type matches
  if (Type != CONFIG_TYPE_STRING) {
    return EFI_ABORTED;
  }
  *String = Value.String;
  return EFI_SUCCESS;
}
// ConfigSGetString
/// Get a string configuration value
/// @param Path   The path of the configuration value
/// @param String On output, the string configuration value
/// @param ...    The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path or String is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigSGetString (
  IN  CHAR16  *Path,
  OUT CHAR16 **String,
  IN  ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, String);
  Status = ConfigVSGetString(Path, String, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSGetString
/// Get a string configuration value
/// @param Path   The path of the configuration value
/// @param String On output, the string configuration value
/// @param Args   The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path or String is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigVSGetString (
  IN  CHAR16   *Path,
  OUT CHAR16  **String,
  IN  VA_LIST   Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if ((Path == NULL) || (String == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Get the value
  Status = ConfigGetString(FullPath, String);
  FreePool(FullPath);
  return Status;
}

// ConfigGetData
/// Get a data configuration value
/// @param Path The path of the configuration value
/// @param Size On output, the data configuration value size in bytes
/// @param Data On output, the data configuration value
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path, Size, or Data is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigGetData (
  IN  CHAR16  *Path,
  OUT UINTN   *Size,
  OUT VOID   **Data
) {
  EFI_STATUS   Status;
  CONFIG_VALUE Value;
  CONFIG_TYPE  Type = CONFIG_TYPE_UNKNOWN;
  // Check parameters
  if ((Path == NULL) || (Size == NULL) || (Data == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Get the configuration value
  Status = ConfigGetValue(Path, &Type, &Value);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  // Check type matches
  if (Type != CONFIG_TYPE_DATA) {
    return EFI_ABORTED;
  }
  *Size = Value.Data.Size;
  *Data = Value.Data.Data;
  return EFI_SUCCESS;
}
// ConfigSGetData
/// Get a data configuration value
/// @param Path The path of the configuration value
/// @param Size On output, the data configuration value size in bytes
/// @param Data On output, the data configuration value
/// @param ...  The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path, Size, or Data is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigSGetData (
  IN  CHAR16  *Path,
  OUT UINTN   *Size,
  OUT VOID   **Data,
  IN  ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Data);
  Status = ConfigVSGetData(Path, Size, Data, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSGetData
/// Get a data configuration value
/// @param Path The path of the configuration value
/// @param Size On output, the data configuration value size in bytes
/// @param Data On output, the data configuration value
/// @param Args The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path, Size, or Data is NULL
/// @retval EFI_NOT_FOUND         If the configuration value path was not found
/// @retval EFI_ABORTED           If the configuration value type does not match
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigVSGetData (
  IN  CHAR16   *Path,
  OUT UINTN    *Size,
  OUT VOID    **Data,
  IN  VA_LIST   Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if ((Path == NULL) || (Size == NULL) || (Data == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Get the value
  Status = ConfigGetData(FullPath, Size, Data);
  FreePool(FullPath);
  return Status;
}

// ConfigGetValueWithDefault
/// Get a configuration value with a default fallback value
/// @param Path         The path of the configuration value
/// @param DefaultType  The default type of the configuration value
/// @param DefaultValue The default value of the configuration value
/// @param Type         On output, the type of the configuration value
/// @param Value        On output, the value of the configuration value
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path, DefaultValue, Type or Value is NULL or DefaultType is invalid
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigGetValueWithDefault (
  IN  CHAR16       *Path,
  IN  CONFIG_TYPE   DefaultType,
  IN  CONFIG_VALUE *DefaultValue,
  OUT CONFIG_TYPE  *Type,
  OUT CONFIG_VALUE *Value
) {
  // Check parameters
  if ((Path == NULL) || (DefaultValue == NULL) || (Type == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Check default type is valid
  switch (DefaultType) {
    case CONFIG_TYPE_BOOLEAN:
    case CONFIG_TYPE_INTEGER:
    case CONFIG_TYPE_UNSIGNED:
    case CONFIG_TYPE_STRING:
    case CONFIG_TYPE_DATA:
      break;

    default:
      return EFI_INVALID_PARAMETER;
  }
  // Get value
  if (EFI_ERROR(ConfigGetValue(Path, Type, Value))) {
    // Set default value
    *Type = DefaultType;
    CopyMem(Value, DefaultValue, sizeof(CONFIG_VALUE));
  }
  return EFI_SUCCESS;
}
// ConfigSGetValueWithDefault
/// Get a configuration value with a default fallback value
/// @param Path         The path of the configuration value
/// @param DefaultType  The default type of the configuration value
/// @param DefaultValue The default value of the configuration value
/// @param Type         On output, the type of the configuration value
/// @param Value        On output, the value of the configuration value
/// @param ...          The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path, DefaultValue, Type or Value is NULL or DefaultType is invalid
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigSGetValueWithDefault (
  IN  CHAR16       *Path,
  IN  CONFIG_TYPE   DefaultType,
  IN  CONFIG_VALUE *DefaultValue,
  OUT CONFIG_TYPE  *Type,
  OUT CONFIG_VALUE *Value,
  IN  ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Value);
  Status = ConfigVSGetValueWithDefault(Path, DefaultType, DefaultValue, Type, Value, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSGetValueWithDefault
/// Get a configuration value with a default fallback value
/// @param Path         The path of the configuration value
/// @param DefaultType  The default type of the configuration value
/// @param DefaultValue The default value of the configuration value
/// @param Type         On output, the type of the configuration value
/// @param Value        On output, the value of the configuration value
/// @param Args         The argument list
/// @return Whether the configuration value was retrieved or not
/// @retval EFI_INVALID_PARAMETER If Path, DefaultValue, Type or Value is NULL or DefaultType is invalid
/// @retval EFI_ACCESS_DENIED     If the configuration value is protected
/// @retval EFI_SUCCESS           If the configuration value was retrieved successfully
EFI_STATUS
EFIAPI
ConfigVSGetValueWithDefault (
  IN  CHAR16       *Path,
  IN  CONFIG_TYPE   DefaultType,
  IN  CONFIG_VALUE *DefaultValue,
  OUT CONFIG_TYPE  *Type,
  OUT CONFIG_VALUE *Value,
  IN  VA_LIST       Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if ((Path == NULL) || (DefaultValue == NULL) || (Type == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Get the value
  Status = ConfigGetValueWithDefault(FullPath, DefaultType, DefaultValue, Type, Value);
  FreePool(FullPath);
  return Status;
}

// ConfigGetBooleanWithDefault
/// Get a boolean configuration value with default
/// @param Path           The path of the configuration value
/// @param DefaultBoolean The default boolean configuration value
/// @return The boolean configuration value
BOOLEAN
EFIAPI
ConfigGetBooleanWithDefault (
  IN CHAR16  *Path,
  IN BOOLEAN  DefaultBoolean
) {
  CONFIG_TYPE  Type = CONFIG_TYPE_UNKNOWN;
  CONFIG_VALUE Value;
  if (EFI_ERROR(ConfigGetValue(Path, &Type, &Value)) || (Type != CONFIG_TYPE_BOOLEAN)) {
    return DefaultBoolean;
  }
  return Value.Boolean;
}
// ConfigSGetBooleanWithDefault
/// Get a boolean configuration value with default
/// @param Path           The path of the configuration value
/// @param DefaultBoolean The default boolean configuration value
/// @param ...            The argument list
/// @return The boolean configuration value
BOOLEAN
EFIAPI
ConfigSGetBooleanWithDefault (
  IN CHAR16  *Path,
  IN BOOLEAN  DefaultBoolean,
  IN ...
) {
  BOOLEAN Result;
  VA_LIST Args;
  VA_START(Args, DefaultBoolean);
  Result = ConfigVSGetBooleanWithDefault(Path, DefaultBoolean, Args);
  VA_END(Args);
  return Result;
}
// ConfigVSGetBooleanWithDefault
/// Get a boolean configuration value with default
/// @param Path           The path of the configuration value
/// @param DefaultBoolean The default boolean configuration value
/// @param Args           The argument list
/// @return The boolean configuration value
BOOLEAN
EFIAPI
ConfigVSGetBooleanWithDefault (
  IN CHAR16  *Path,
  IN BOOLEAN  DefaultBoolean,
  IN VA_LIST  Args
) {
  BOOLEAN  Result;
  CHAR16  *FullPath;
  // Check parameters
  if (Path == NULL) {
    return DefaultBoolean;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return DefaultBoolean;
  }
  // Get the value
  Result = ConfigGetBooleanWithDefault(FullPath, DefaultBoolean);
  FreePool(FullPath);
  return Result;
}

// ConfigGetIntegerWithDefault
/// Get an integer configuration value with default
/// @param Path           The path of the configuration value
/// @param DefaultInteger The default integer configuration value
/// @return The integer configuration value
INTN
EFIAPI
ConfigGetIntegerWithDefault (
  IN CHAR16 *Path,
  IN INTN    DefaultInteger
) {
  CONFIG_TYPE  Type = CONFIG_TYPE_UNKNOWN;
  CONFIG_VALUE Value;
  if (EFI_ERROR(ConfigGetValue(Path, &Type, &Value)) || (Type != CONFIG_TYPE_INTEGER)) {
    return DefaultInteger;
  }
  return Value.Integer;
}
// ConfigSGetIntegerWithDefault
/// Get an integer configuration value with default
/// @param Path           The path of the configuration value
/// @param DefaultInteger The default integer configuration value
/// @param ...            The argument list
/// @return The integer configuration value
INTN
EFIAPI
ConfigSGetIntegerWithDefault (
  IN CHAR16 *Path,
  IN INTN    DefaultInteger,
  IN ...
) {
  INTN    Result;
  VA_LIST Args;
  VA_START(Args, DefaultInteger);
  Result = ConfigVSGetIntegerWithDefault(Path, DefaultInteger, Args);
  VA_END(Args);
  return Result;
}
// ConfigVSGetIntegerWithDefault
/// Get an integer configuration value with default
/// @param Path           The path of the configuration value
/// @param DefaultInteger The default integer configuration value
/// @param Args           The argument list
/// @return The integer configuration value
INTN
EFIAPI
ConfigVSGetIntegerWithDefault (
  IN CHAR16  *Path,
  IN INTN     DefaultInteger,
  IN VA_LIST  Args
) {
  INTN    Result;
  CHAR16 *FullPath;
  // Check parameters
  if (Path == NULL) {
    return DefaultInteger;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return DefaultInteger;
  }
  // Get the value
  Result = ConfigGetIntegerWithDefault(FullPath, DefaultInteger);
  FreePool(FullPath);
  return Result;
}

// ConfigGetUnsignedWithDefault
/// Get an unsigned integer configuration value with default
/// @param Path           The path of the configuration value
/// @param DefaulUnsigned The default unsigned configuration value
/// @return The unsigned integer configuration value
UINTN
EFIAPI
ConfigGetUnsignedWithDefault (
  IN CHAR16 *Path,
  IN UINTN   DefaultUnsigned
) {
  CONFIG_TYPE  Type = CONFIG_TYPE_UNKNOWN;
  CONFIG_VALUE Value;
  if (EFI_ERROR(ConfigGetValue(Path, &Type, &Value)) || (Type != CONFIG_TYPE_UNSIGNED)) {
    return DefaultUnsigned;
  }
  return Value.Unsigned;
}
// ConfigSGetUnsignedWithDefault
/// Get an unsigned integer configuration value with default
/// @param Path           The path of the configuration value
/// @param DefaulUnsigned The default unsigned configuration value
/// @param ...            The argument list
/// @return The unsigned integer configuration value
UINTN
EFIAPI
ConfigSGetUnsignedWithDefault (
  IN CHAR16 *Path,
  IN UINTN   DefaultUnsigned,
  IN ...
) {
  UINTN   Result;
  VA_LIST Args;
  VA_START(Args, DefaultUnsigned);
  Result = ConfigVSGetUnsignedWithDefault(Path, DefaultUnsigned, Args);
  VA_END(Args);
  return Result;
}
// ConfigVSGetUnsignedWithDefault
/// Get an unsigned integer configuration value with default
/// @param Path           The path of the configuration value
/// @param DefaulUnsigned The default unsigned configuration value
/// @param Args           The argument list
/// @return The unsigned integer configuration value
UINTN
EFIAPI
ConfigVSGetUnsignedWithDefault (
  IN CHAR16  *Path,
  IN UINTN    DefaultUnsigned,
  IN VA_LIST  Args
) {
  UINTN   Result;
  CHAR16 *FullPath;
  // Check parameters
  if (Path == NULL) {
    return DefaultUnsigned;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return DefaultUnsigned;
  }
  // Get the value
  Result = ConfigGetUnsignedWithDefault(FullPath, DefaultUnsigned);
  FreePool(FullPath);
  return Result;
}

// ConfigGetStringWithDefault
/// Get a string configuration value with default
/// @param Path          The path of the configuration value
/// @param DefaultString The default string configuration value
/// @return The string configuration value
CHAR16 *
EFIAPI
ConfigGetStringWithDefault (
  IN CHAR16 *Path,
  IN CHAR16 *DefaultString
) {
  CONFIG_TYPE  Type = CONFIG_TYPE_UNKNOWN;
  CONFIG_VALUE Value;
  if (EFI_ERROR(ConfigGetValue(Path, &Type, &Value)) || (Type != CONFIG_TYPE_STRING)) {
    return DefaultString;
  }
  return Value.String;
}
// ConfigSGetStringWithDefault
/// Get a string configuration value with default
/// @param Path          The path of the configuration value
/// @param DefaultString The default string configuration value
/// @param ...           The argument list
/// @return The string configuration value
CHAR16 *
EFIAPI
ConfigSGetStringWithDefault (
  IN CHAR16 *Path,
  IN CHAR16 *DefaultString,
  IN ...
) {
  CHAR16  *Result;
  VA_LIST  Args;
  VA_START(Args, DefaultString);
  Result = ConfigVSGetStringWithDefault(Path, DefaultString, Args);
  VA_END(Args);
  return Result;
}
// ConfigVSGetStringWithDefault
/// Get a string configuration value with default
/// @param Path          The path of the configuration value
/// @param DefaultString The default string configuration value
/// @param Args          The argument list
/// @return The string configuration value
CHAR16 *
EFIAPI
ConfigVSGetStringWithDefault (
  IN CHAR16  *Path,
  IN CHAR16  *DefaultString,
  IN VA_LIST  Args
) {
  CHAR16 *Result;
  CHAR16 *FullPath;
  // Check parameters
  if (Path == NULL) {
    return DefaultString;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return DefaultString;
  }
  // Get the value
  Result = ConfigGetStringWithDefault(FullPath, DefaultString);
  FreePool(FullPath);
  return Result;
}

// ConfigGetDataWithDefault
/// Get a data configuration value with default
/// @param Path        The path of the configuration value
/// @param DefaultSize The default data configuration value size
/// @param DefaultData The default data configuration value
/// @param Size        On output, the data configuration value size
/// @return The data configuration value
VOID *
EFIAPI
ConfigGetDataWithDefault (
  IN  CHAR16 *Path,
  IN  UINTN   DefaultSize,
  IN  VOID   *DefaultData,
  OUT UINTN  *Size
) {
  CONFIG_TYPE  Type = CONFIG_TYPE_UNKNOWN;
  CONFIG_VALUE Value;
  if (EFI_ERROR(ConfigGetValue(Path, &Type, &Value)) || (Type != CONFIG_TYPE_DATA)) {
    *Size = DefaultSize;
    return DefaultData;
  }
  *Size = Value.Data.Size;
  return Value.Data.Data;
}
// ConfigSGetDataWithDefault
/// Get a data configuration value with default
/// @param Path        The path of the configuration value
/// @param DefaultSize The default data configuration value size
/// @param DefaultData The default data configuration value
/// @param Size        On output, the data configuration value size
/// @param ...         The argument list
/// @return The data configuration value
VOID *
EFIAPI
ConfigSGetDataWithDefault (
  IN  CHAR16 *Path,
  IN  UINTN   DefaultSize,
  IN  VOID   *DefaultData,
  OUT UINTN  *Size,
  IN  ...
) {
  VOID    *Result;
  VA_LIST  Args;
  VA_START(Args, Size);
  Result = ConfigVSGetDataWithDefault(Path, DefaultSize, DefaultData, Size, Args);
  VA_END(Args);
  return Result;
}
// ConfigVSGetDataWithDefault
/// Get a data configuration value with default
/// @param Path        The path of the configuration value
/// @param DefaultSize The default data configuration value size
/// @param DefaultData The default data configuration value
/// @param Size        On output, the data configuration value size
/// @param Args        The argument list
/// @return The data configuration value
VOID *
EFIAPI
ConfigVSGetDataWithDefault (
  IN  CHAR16  *Path,
  IN  UINTN    DefaultSize,
  IN  VOID    *DefaultData,
  OUT UINTN   *Size,
  IN  VA_LIST  Args
) {
  VOID   *Result;
  CHAR16 *FullPath;
  // Check parameters
  if ((Path == NULL) || (Size == NULL)) {
    *Size = DefaultSize;
    return DefaultData;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    *Size = DefaultSize;
    return DefaultData;
  }
  // Get the value
  Result = ConfigGetDataWithDefault(FullPath, DefaultSize, DefaultData, Size);
  FreePool(FullPath);
  return Result;
}

// ConfigSetValue
/// Set a configuration value
/// @param Path  The path of the configuration value
/// @param Type  The configuration type to set
/// @param Value The configuration value to set
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path or Value is NULL or Type is invalid
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigSetValue (
  IN CHAR16       *Path,
  IN CONFIG_TYPE   Type,
  IN CONFIG_VALUE *Value
) {
  EFI_STATUS   Status;
  CONFIG_TREE *Node = NULL;
  // Check parameters
  if ((Path == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  switch (Type) {
    case CONFIG_TYPE_BOOLEAN:
    case CONFIG_TYPE_INTEGER:
    case CONFIG_TYPE_UNSIGNED:
      break;

    case CONFIG_TYPE_STRING:
      if (Value->String != NULL) {
        break;
      } else

    case CONFIG_TYPE_DATA:
      if ((Value->Data.Data != NULL) && (Value->Data.Size != 0)) {
        break;
      }

    default:
      return EFI_INVALID_PARAMETER;
  }
  // Use configuration protocol if present
  if ((mConfig != NULL) && (mConfig->SetValue != NULL)) {
    return mConfig->SetValue(Path, Type, Value);
  }
  // Get configuration tree node
  Status = ConfigFind(Path, TRUE, &Node);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  if (Node == NULL) {
    return EFI_NOT_FOUND;
  }
  if (Node->Children != NULL) {
    return EFI_ACCESS_DENIED;
  }
  // Set type and value
  Node->Type = Type;
  if (Type == CONFIG_TYPE_STRING) {
    // Duplicate string type
    Node->Value.String = StrDup(Value->String);
    if (Node->Value.String == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
  } else if (Type == CONFIG_TYPE_DATA) {
    // Duplicate data type
    Node->Value.Data.Size = Value->Data.Size;
    Node->Value.Data.Data = AllocateZeroPool(Value->Data.Size);
    if (Node->Value.Data.Data == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    CopyMem(Node->Value.Data.Data, Value->Data.Data, Value->Data.Size);
  } else {
    // Copy all other types
    CopyMem(&(Node->Value), Value, sizeof(CONFIG_VALUE));
  }
  return EFI_SUCCESS;
}
// ConfigSSetValue
/// Set a configuration value
/// @param Path  The path of the configuration value
/// @param Type  The configuration type to set
/// @param Value The configuration value to set
/// @param ...   The argument list
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path or Value is NULL or Type is invalid
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigSSetValue (
  IN CHAR16       *Path,
  IN CONFIG_TYPE   Type,
  IN CONFIG_VALUE *Value,
  IN ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Value);
  Status = ConfigVSSetValue(Path, Type, Value, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSSetValue
/// Set a configuration value
/// @param Path  The path of the configuration value
/// @param Type  The configuration type to set
/// @param Value The configuration value to set
/// @param Args  The argument list
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path or Value is NULL or Type is invalid
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigVSSetValue (
  IN CHAR16       *Path,
  IN CONFIG_TYPE   Type,
  IN CONFIG_VALUE *Value,
  IN VA_LIST       Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if ((Path == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Set the value
  Status = ConfigSetValue(FullPath, Type, Value);
  FreePool(FullPath);
  return Status;
}

// ConfigSetBoolean
/// Set a boolean configuration value
/// @param Path    The path of the configuration value
/// @param Boolean The boolean configuration value to set
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigSetBoolean (
  IN CHAR16  *Path,
  IN BOOLEAN  Boolean
) {
  CONFIG_VALUE Value;
  Value.Boolean = Boolean;
  return ConfigSetValue(Path, CONFIG_TYPE_BOOLEAN, &Value);
}
// ConfigSSetBoolean
/// Set a boolean configuration value
/// @param Path    The path of the configuration value
/// @param Boolean The boolean configuration value to set
/// @param ...     The argument list
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigSSetBoolean (
  IN CHAR16  *Path,
  IN BOOLEAN  Boolean,
  IN ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Boolean);
  Status = ConfigVSSetBoolean(Path, Boolean, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSSetBoolean
/// Set a boolean configuration value
/// @param Path    The path of the configuration value
/// @param Boolean The boolean configuration value to set
/// @param Args    The argument list
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigVSSetBoolean (
  IN CHAR16  *Path,
  IN BOOLEAN  Boolean,
  IN VA_LIST  Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if (Path == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Set the value
  Status = ConfigSetBoolean(FullPath, Boolean);
  FreePool(FullPath);
  return Status;
}

// ConfigSetInteger
/// Set an integer configuration value
/// @param Path    The path of the configuration value
/// @param Integer The integer configuration value to set
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigSetInteger (
  IN CHAR16 *Path,
  IN INTN    Integer
) {
  CONFIG_VALUE Value;
  Value.Integer = Integer;
  return ConfigSetValue(Path, CONFIG_TYPE_INTEGER, &Value);
}
// ConfigSSetInteger
/// Set an integer configuration value
/// @param Path    The path of the configuration value
/// @param Integer The integer configuration value to set
/// @param ...     The argument list
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigSSetInteger (
  IN CHAR16 *Path,
  IN INTN    Integer,
  IN ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Integer);
  Status = ConfigVSSetInteger(Path, Integer, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSSetInteger
/// Set an integer configuration value
/// @param Path    The path of the configuration value
/// @param Integer The integer configuration value to set
/// @param Args    The argument list
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigVSSetInteger (
  IN CHAR16  *Path,
  IN INTN     Integer,
  IN VA_LIST  Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if (Path == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Set the value
  Status = ConfigSetInteger(FullPath, Integer);
  FreePool(FullPath);
  return Status;
}

// ConfigSetUnsigned
/// Set an unsigned integer configuration value
/// @param Path     The path of the configuration value
/// @param Unsigned The unsigned integer configuration value to set
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigSetUnsigned (
  IN CHAR16 *Path,
  IN UINTN   Unsigned
) {
  CONFIG_VALUE Value;
  Value.Unsigned = Unsigned;
  return ConfigSetValue(Path, CONFIG_TYPE_UNSIGNED, &Value);
}
// ConfigSSetUnsigned
/// Set an unsigned integer configuration value
/// @param Path     The path of the configuration value
/// @param Unsigned The unsigned integer configuration value to set
/// @param ...      The argument list
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigSSetUnsigned (
  IN CHAR16 *Path,
  IN UINTN   Unsigned,
  IN ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Unsigned);
  Status = ConfigVSSetUnsigned(Path, Unsigned, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSSetUnsigned
/// Set an unsigned integer configuration value
/// @param Path     The path of the configuration value
/// @param Unsigned The unsigned integer configuration value to set
/// @param Args     The argument list
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigVSSetUnsigned (
  IN CHAR16  *Path,
  IN UINTN    Unsigned,
  IN VA_LIST  Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if (Path == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Set the value
  Status = ConfigSetUnsigned(FullPath, Unsigned);
  FreePool(FullPath);
  return Status;
}

// ConfigSetString
/// Set a string configuration value
/// @param Path   The path of the configuration value
/// @param String The string configuration value to set
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path or String is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigSetString (
  IN CHAR16 *Path,
  IN CHAR16 *String
) {
  CONFIG_VALUE Value;
  Value.String = String;
  return ConfigSetValue(Path, CONFIG_TYPE_STRING, &Value);
}
// ConfigSSetString
/// Set a string configuration value
/// @param Path   The path of the configuration value
/// @param String The string configuration value to set
/// @param ...    The argument list
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path or String is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigSSetString (
  IN CHAR16 *Path,
  IN CHAR16 *String,
  IN ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, String);
  Status = ConfigVSSetString(Path, String, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSSetString
/// Set a string configuration value
/// @param Path   The path of the configuration value
/// @param String The string configuration value to set
/// @param Args   The argument list
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path or String is NULL
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigVSSetString (
  IN CHAR16  *Path,
  IN CHAR16  *String,
  IN VA_LIST  Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if ((Path == NULL) || (String == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Set the value
  Status = ConfigSetString(FullPath, String);
  FreePool(FullPath);
  return Status;
}

// ConfigSetData
/// Set a data configuration value
/// @param Path The path of the configuration value
/// @param Size The size of the data configuration value
/// @param Data The data configuration value to set
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path or Data is NULL or Size is zero
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigSetData (
  IN CHAR16 *Path,
  IN UINTN   Size,
  IN VOID   *Data
) {
  CONFIG_VALUE Value;
  Value.Data.Size = Size;
  Value.Data.Data = Data;
  return ConfigSetValue(Path, CONFIG_TYPE_DATA, &Value);
}
// ConfigSSetData
/// Set a data configuration value
/// @param Path The path of the configuration value
/// @param Size The size of the data configuration value
/// @param Data The data configuration value to set
/// @param ...  The argument list
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path or Data is NULL or Size is zero
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigSSetData (
  IN CHAR16 *Path,
  IN UINTN   Size,
  IN VOID   *Data,
  IN ...
) {
  EFI_STATUS Status;
  VA_LIST    Args;
  VA_START(Args, Data);
  Status = ConfigVSSetData(Path, Size, Data, Args);
  VA_END(Args);
  return Status;
}
// ConfigVSSetData
/// Set a data configuration value
/// @param Path The path of the configuration value
/// @param Size The size of the data configuration value
/// @param Data The data configuration value to set
/// @param Args The argument list
/// @return Whether the configuration value was set or not
/// @retval EFI_INVALID_PARAMETER If Path or Data is NULL or Size is zero
/// @retval EFI_OUT_OF_RESOURCES  If memory could not be allocated for configuration data
/// @retval EFI_SUCCESS           If the configuration value was set successfully
EFI_STATUS
EFIAPI
ConfigVSSetData (
  IN CHAR16  *Path,
  IN UINTN    Size,
  IN VOID    *Data,
  IN VA_LIST  Args
) {
  EFI_STATUS  Status;
  CHAR16     *FullPath;
  // Check parameters
  if ((Path == NULL) || (Data == NULL) || (Size == 0)) {
    return EFI_INVALID_PARAMETER;
  }
  // Create the path from the argument list
  FullPath = CatVSPrint(NULL, Path, Args);
  if (FullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Set the value
  Status = ConfigSetData(FullPath, Size, Data);
  FreePool(FullPath);
  return Status;
}

// mConfigProtocol
/// The configuration protocol
STATIC CONFIG_PROTOCOL mConfigProtocol = {
  ConfigParseXml,
  ConfigPartialFree,
  ConfigGetList,
  ConfigGetValue,
  ConfigSetValue,
};

// ConfigLibInitialize
/// Configuration library initialize use
/// @return Whether the configuration initialized successfully or not
/// @retval EFI_SUCCESS The configuration successfully initialized
EFI_STATUS
EFIAPI
ConfigLibInitialize (
  VOID
) {
  EFI_STATUS Status;
  // Check if configuration protocol is already installed
  mConfig = NULL;
  if (!EFI_ERROR(gBS->LocateProtocol(&mConfigGuid, NULL, (VOID **)&mConfig)) && (mConfig != NULL)) {
    return EFI_SUCCESS;
  }
  // Load configuration
  Status = ConfigLoad(NULL, CONFIG_FILE);
  if (EFI_ERROR(Status)) {
    // If not found then load architecture configuration
    Status = ConfigLoad(NULL, CONFIG_ARCH_FILE);
  }
  // Install configuration protocol
  mConfigHandle = NULL;
  mConfigProtocol.Parse = ConfigParseXml;
  mConfigProtocol.Free = ConfigPartialFree;
  mConfigProtocol.GetList = ConfigGetList;
  mConfigProtocol.GetValue = ConfigGetValue;
  mConfigProtocol.SetValue = ConfigSetValue;
  return gBS->InstallMultipleProtocolInterfaces(&mConfigHandle, &mConfigGuid, (VOID *)&mConfigProtocol, NULL);
}

// ConfigLibFinish
/// Configuration library finish use
/// @return Whether the configuration was finished successfully or not
/// @retval EFI_SUCCESS The configuration successfully finished
EFI_STATUS
EFIAPI
ConfigLibFinish (
  VOID
) {
  // Uninstall configuration protocol
  mConfig = NULL;
  if (mConfigHandle != NULL) {
    gBS->UninstallMultipleProtocolInterfaces(mConfigHandle, &mConfigGuid, (VOID *)&mConfigProtocol, NULL);
    mConfigHandle = NULL;
  }
  // Free all configuration tree nodes
  return ConfigFree();
}
