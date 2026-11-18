#include "EnginePCH.h"
#include "Object.h"

#include "Core/EngineStatics.h"
#include "UObject/Class.h"

#include "Serialization/TypeSerializer.h"
#include "Serialization/MathSerialization.h"
#include "Serialization/Formatter/BinaryArchiveFormatter.h"



TArray<UObject*> GUObjectArray;

void UObjectBase::SetOuter(UObject* InOuter)
{
	if (Outer == InOuter)
		return;
	// 검색표에 있던 객체만 새 키로 다시 넣는다. 등록되지 않은 객체(UObject(false))는 건드리지 않는다.
	UObject* Self = static_cast<UObject*>(this);
	const bool bWasHashed = UnhashObjectName(Self);
	Outer = InOuter;
	if (bWasHashed)
		HashObjectName(Self);
}

void UObjectBase::SetName(const FName& InName)
{
	if (Name == InName)
		return;
	UObject* Self = static_cast<UObject*>(this);
	const bool bWasHashed = UnhashObjectName(Self);
	Name = FName(InName);
	if (bWasHashed)
		HashObjectName(Self);
}

UObject::UObject()
{
	ObjectUUID = FEngineStatics::GetUUID();
	InternalIndex = GUObjectArray.Num();
	GUObjectArray.Add(this);
	// Outer/Name은 StaticAllocateObject가 생성자보다 먼저 채워 두었다.
	HashObjectName(this);
}

UObject::UObject(bool bRegister)
{
	bIsRegistered = bRegister;
}

UObject::~UObject()
{
	if (bIsRegistered)
	{
		if (GetClass())
		{
			UnhashObject(this, GetClass());
		}
		UnhashObjectName(this);
		int32 LastIndex = GUObjectArray.Num() - 1;
		if (InternalIndex != LastIndex)
		{
			UObject* MovedObject = GUObjectArray[LastIndex];
			GUObjectArray[InternalIndex] = MovedObject;
			MovedObject->InternalIndex = InternalIndex; // 옮겨간 오브젝트 인덱스 갱신
		}
		GUObjectArray.RemoveLast();
	}
}

UClass* UObject::StaticClass()
{
	static UClass c;
	static bool bIsInit = false;
	if (!bIsInit)
	{
		c.Name = "Object";
		c.Super = nullptr;
		c.Size = sizeof(UObject);
		c.Constructor = [](void* Memory) -> UObject*
			{
				return ::new (Memory) UObject();
			};
		bIsInit = true;
	}
	return &c;
}

bool UObject::IsA(const UClass* Class) const
{
	const UClass* ThisClass = GetClass();
	return ThisClass && ThisClass->IsChildOf(Class);
}

void UObject::Serialize(FArchive& Ar)
{
	FBinaryArchiveFormatter Formatter(Ar);
	FStructuredArchive StructuredAr(Formatter);
	Serialize(StructuredAr.Open().EnterRecord());
	StructuredAr.Close();
}

void UObject::Serialize(FStructuredArchive::FRecord Record)
{
	SerializeScriptProperties(Record.EnterField("Properties"));
}

void UObject::SerializeScriptProperties(FStructuredArchive::FSlot Slot)
{
	GetClass()->SerializeTaggedProperties(Slot, this);
}



