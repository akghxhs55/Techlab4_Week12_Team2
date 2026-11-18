#include "EnginePCH.h"
#include "UObjectHash.h"

#include "UObject/Object.h"
#include "UObject/Class.h"

#include <unordered_map>

//클래스에 해당하는 UObject들의 리스트(O(1)에 바로 가져옴)
static TMap<const UClass*, TSet<UObject*>>& GetClassToObjects() { static TMap<const UClass*, TSet<UObject*>> M; return M; }

//bIsIncludeDerivedClass가 켜졌을 때 자식 클래스 목록이 필요
static TMap<const UClass*, TSet<UClass*>>& GetClassToChildren() { static TMap<const UClass*, TSet<UClass*>> M; return M; }

static TMap<FString, UClass*>& GetClassMap()
{
	static TMap<FString, UClass* > Map;
	return Map;
}

namespace
{
	struct FObjectNameKey
	{
		const UObject* Outer;
		FName Name;

		bool operator==(const FObjectNameKey& Other) const { return Outer == Other.Outer && Name == Other.Name; }
	};

	struct FObjectNameKeyHash
	{
		size_t operator()(const FObjectNameKey& Key) const noexcept
		{
			const size_t OuterHash = std::hash<const void*>{}(Key.Outer);
			return OuterHash ^ (FNameHash{}(Key.Name) + 0x9e3779b97f4a7c15ull + (OuterHash << 6) + (OuterHash >> 2));
		}
	};

	// 대부분 버킷에 객체가 하나뿐이다. 같은 이름이 잠깐 겹치는 경우(이름 변경 도중 등)를 위해 배열로 둔다.
	std::unordered_map<FObjectNameKey, TArray<UObject*>, FObjectNameKeyHash>& GetOuterNameToObjects()
	{
		static std::unordered_map<FObjectNameKey, TArray<UObject*>, FObjectNameKeyHash> M;
		return M;
	}
}

void HashObjectName(UObject* Obj)
{
	if (!Obj) return;
	GetOuterNameToObjects()[FObjectNameKey{ Obj->GetOuter(), Obj->GetFName() }].AddUnique(Obj);
}

bool UnhashObjectName(UObject* Obj)
{
	if (!Obj) return false;
	auto& Map = GetOuterNameToObjects();
	const auto It = Map.find(FObjectNameKey{ Obj->GetOuter(), Obj->GetFName() });
	if (It == Map.end()) return false;

	TArray<UObject*>& Bucket = It->second;
	const int32 Index = Bucket.Find(Obj);
	if (Index == INDEX_NONE) return false;

	Bucket.RemoveAtSwap(static_cast<uint32>(Index));
	if (Bucket.IsEmpty())
		Map.erase(It);
	return true;
}

const TArray<UObject*>* FindObjectsWithOuterAndName(const UObject* Outer, FName Name)
{
	const auto& Map = GetOuterNameToObjects();
	const auto It = Map.find(FObjectNameKey{ Outer, Name });
	return It != Map.end() ? &It->second : nullptr;
}

void RegisterClass(UClass* Class)
{
	GetClassMap()[Class->Name] = Class;
	if (Class->Super)
	{
		GetClassToChildren()[Class->Super].Add(Class);
	}
}

UClass* FindClass(const FString& Name)
{
	const FString Lookup = Name == "ASkyLightActor" ? "AAmbientLightActor" :
		Name == "USkyLightComponent" ? "UAmbientLightComponent" : Name;
	UClass** Found = GetClassMap().Find(Lookup);
	return Found ? *Found : nullptr;
}

UObject* FindObjectWithOuter(const UObject* outer, const UClass* Class, FName Name)
{
	if (const TArray<UObject*>* Bucket = FindObjectsWithOuterAndName(outer, Name))
	{
		for (UObject* Obj : *Bucket)
		{
			if (Obj && Obj->GetClass() == Class)
				return Obj;
		}
	}
	return nullptr;
}

namespace
{
	// 결과에 들어갈 객체 수. 전체 객체 수로 예약하면 이터레이터 하나가 GUObjectArray만큼 메모리를 잡는다.
	int32 CountObjectsOfClass(const UClass* ClassToLookFor, bool bIsIncludeDerivedClass)
	{
		int32 Count = 0;
		if (const TSet<UObject*>* Objects = GetClassToObjects().Find(ClassToLookFor))
			Count += static_cast<int32>(Objects->Num());

		if (bIsIncludeDerivedClass)
			if (const TSet<UClass*>* ChildClasses = GetClassToChildren().Find(ClassToLookFor))
				for (UClass* Child : *ChildClasses)
					Count += CountObjectsOfClass(Child, true);
		return Count;
	}

	void AppendObjectsOfClass(const UClass* ClassToLookFor, TArray<UObject*>& Results, bool bIsIncludeDerivedClass)
	{
		//Class에 해당하는 Objects 리스트를 가져온다.
		if (const TSet<UObject*>* Objects = GetClassToObjects().Find(ClassToLookFor))
		{
			for (UObject* Obj : *Objects)
			{
				Results.Add(Obj);
			}
		}

		if (!bIsIncludeDerivedClass) return;

		//자식 클래스인 목록을 가져온다
		if (const TSet<UClass*>* ChildClasses = GetClassToChildren().Find(ClassToLookFor))
		{
			for (UClass* Child : *ChildClasses)
			{
				AppendObjectsOfClass(Child, Results, true);
			}
		}
	}
}

void GetObjectsOfClass(const UClass* ClassToLookFor, TArray<UObject*>& Results, bool bIsIncludeDerivedClass)
{
	if (!ClassToLookFor) return;

	// 정확한 개수만큼 한 번만 예약하고, 재귀 단계마다 재할당하지 않는다.
	Results.Reserve(static_cast<uint32>(Results.Num() + CountObjectsOfClass(ClassToLookFor, bIsIncludeDerivedClass)));
	AppendObjectsOfClass(ClassToLookFor, Results, bIsIncludeDerivedClass);
}

void HashObject(UObject* Obj, const UClass* Class)
{
	GetClassToObjects()[Class].Add(Obj);
}

void UnhashObject(UObject* Obj, const UClass* Class)
{
	if (TSet<UObject*>* Objects = GetClassToObjects().Find(Class))
		Objects->Remove(Obj);
}

void GetDerivedClasses(const UClass* ClassToLookFor, TArray<UClass*>& Results, bool bRecursive)
{
	if(const TSet<UClass*>* Children = GetClassToChildren().Find(ClassToLookFor))
		for (UClass* Child : *Children)
		{
			Results.Add(Child);
			if (bRecursive) GetDerivedClasses(Child, Results, true);
		}
}
bool TryGetLiveObjectUUID(const UObject* Object, const UClass* ObjectClass, uint32& OutUUID)
{
	OutUUID = 0;

	if (!Object)
		return false;

	// 클래스 정보를 알고 있다면 빠른 검색표를 사용한다.
	if (ObjectClass)
	{
		const TSet<UObject*>* Objects =
			GetClassToObjects().Find(ObjectClass);

		if (!Objects)
			return false;

		// 기존 집합이 UObject*를 보관하므로 타입만 맞춘다.
		// 객체를 수정하는 용도로 사용하지 않는다.
		UObject* ObjectKey = const_cast<UObject*>(Object);

		// 주소가 등록되어 있는지 먼저 확인한다.
		if (!Objects->Contains(ObjectKey))
			return false;

		// 존재를 확인한 뒤에만 객체 내부를 읽는다.
		OutUUID = Object->GetUUID();
		return true;
	}

	// 클래스 정보가 없는 객체는 기존 방식으로 확인한다.
	// 예: 생성자 안에서 구독했거나 직접 new로 만든 객체
	for (const UObject* LiveObject : GUObjectArray)
	{
		if (LiveObject && LiveObject == Object)
		{
			OutUUID = LiveObject->GetUUID();
			return true;
		}
	}

	return false;
}