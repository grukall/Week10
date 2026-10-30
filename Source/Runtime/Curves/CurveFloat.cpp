#include "EnginePCH.h"

#include "Curves/CurveFloat.h"
#include "UObject/UObjectGlobals.h"

bool UCurveFloat::SaveToFile(const FString& Path)
{
	json Root = json::object();
	FloatCurve.Serialize(Root, false);

	std::ofstream Out(Path);
	if (!Out.is_open())
	{
		HTR_LOG(Error, "[Curve] failed to write: {}", Path);
		return false;
	}
	Out << Root.dump(2) << '\n';
	return true;
}

UCurveFloat* UCurveFloat::LoadFromFile(const FString& Path)
{
	// 같은 파일은 한 번만 만들고 포인터를 돌려준다
	static TMap<FString, UCurveFloat*> Cache;
	if (UCurveFloat** Found = Cache.Find(Path))
		return *Found;

	std::ifstream File(Path);
	if (!File.is_open())
	{
		HTR_LOG(Warning, "[Curve] file not found: {}", Path);
		return nullptr;
	}

	json Root = json::parse(File, nullptr, false);
	if (Root.is_discarded() || !Root.is_object())
	{
		HTR_LOG(Warning, "[Curve] invalid json: {}", Path);
		return nullptr;
	}

	UCurveFloat* Curve = NewObject<UCurveFloat>();
	Curve->FloatCurve.Serialize(Root, true);
	Cache.Add(Path, Curve);
	return Curve;
}
