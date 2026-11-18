#pragma once

#include "Core/NameTypes.h"
#include "Containers/Array.h"

class UObject;
class UClass;

void RegisterClass(UClass* Class);

UClass* FindClass(const FString& Name);

UObject* FindObjectWithOuter(const UObject* outer, const UClass* Class, FName Name);

void GetObjectsOfClass(const UClass* ClassToLookFor, TArray<UObject*>& Results, bool bIsIncludeDerivedClass = true);

void HashObject(UObject* Obj, const UClass* Class);
void UnhashObject(UObject* Obj, const UClass* Class);

// (Outer, Name) 검색표. 이름 검색을 GUObjectArray 전체 순회 없이 처리한다.
void HashObjectName(UObject* Obj);
// 검색표에 있었으면 true. 이름/Outer 변경 시 다시 넣을지 판단하는 데 쓴다.
bool UnhashObjectName(UObject* Obj);
// 같은 Outer 아래 같은 이름을 가진 객체들. 없으면 nullptr.
const TArray<UObject*>* FindObjectsWithOuterAndName(const UObject* Outer, FName Name);

void GetDerivedClasses(const UClass* ClassToLookFor, TArray<UClass*>& Results, bool bRecursive = true);

//Delegate용 getter
bool TryGetLiveObjectUUID(const UObject* Object, const UClass* ObjectClass, uint32& OutUUID);