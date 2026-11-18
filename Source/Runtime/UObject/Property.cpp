#include "EnginePCH.h"
#include "UObject/Property.h"

#include "Render/Material.h"
#include "UObject/Class.h"

// ---- FColorProperty ----

void FColorProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const
{
    Slot << GetPropertyValue(Value);
}

// ---- FLinearColorProperty ----

void FLinearColorProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const
{
    Slot << GetPropertyValue(Value);
}

// ---- FObjectProperty ----

void FObjectProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const
{
    UObject*& Ref = GetPropertyValue(Value);

    if (PropertyClass == UMaterial::StaticClass())
    {
		UMaterial* Material = Cast<UMaterial>(Ref);
        UMaterial::SerializeMaterialReference(Slot, Material);
        Ref = Material;
        return;
    }

    UObject* Previous = Ref;
    Slot << Ref;

    // 불러온 객체가 프로퍼티가 기대하는 클래스가 아니면 되돌린다
    if (Slot.GetUnderlyingArchive().IsLoading() && Ref && PropertyClass && !Ref->IsA(PropertyClass))
    {
        KRS_LOG(Warning, "Load: '{}' is not a {}, keeping default", Name, PropertyClass->Name);
        Ref = Previous;
    }
}

// ---- FEnumProperty ----

void FEnumProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const
{
    // UENUM은 이름으로 저장한다. enum 항목 순서를 바꿔도 파일이 깨지지 않는다.
    FString EntryName = Enum ? Enum->ToName(GetValue(Value)) : FString();
    Slot << EntryName;

    int64 Loaded = 0;
    if (Slot.GetUnderlyingArchive().IsLoading() && Enum)
    {
        if (Enum->FromName(EntryName, Loaded))
            SetValue(Value, Loaded);
        else
            KRS_LOG(Warning, "Load: '{}' has no enum entry '{}', keeping default", Name, EntryName);
    }
}

// ---- FStructProperty ----

void FStructProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const
{
    Struct->SerializeTaggedProperties(Slot, Value);
}

// ---- FArrayProperty ----
void FArrayProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value) const
{
	if (!Inner)
	{
        return;
	}

    int32 NumElements = Num(Value);
	FStructuredArchive::FArray Array = Slot.EnterArray(NumElements);

    if (Slot.GetUnderlyingArchive().IsLoading())
    {
	    SetNumFn(Value, NumElements);
    }

    for (int32 Index = 0; Index < NumElements; ++Index)
    {
	    Inner->SerializeItem(Array.EnterElement(), GetElementPtr(Value, Index));
    }
}
