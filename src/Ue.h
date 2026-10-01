#pragma once

#include "PEHook.h"
#include "Reflect.h"

// Small helpers over CommonLibOB64 for calling reflected functions by name (game thread only).
namespace ue
{
	inline std::string NameOf(UE::UObject* a_o)
	{
		return a_o ? pe::Utf8(a_o->GetFName().ToString()) : std::string("null");
	}

	template <class T = UE::UObject>
	T* Find(const wchar_t* a_path)
	{
		return static_cast<T*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, a_path));
	}

	inline UE::UStruct* Struct(const wchar_t* a_path)
	{
		return UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, a_path);
	}

	inline UE::UClass* Class(const wchar_t* a_path)
	{
		return UE::StaticFindObject<UE::UClass>(nullptr, nullptr, a_path);
	}

	// ProcessEvent inside __try/__except: a call whose world was torn down under its context returns false instead of
	// taking the game down (2026-09-30, Minimap Menu's crash on quit; gate rule or-world-context-calls-are-guarded)
	inline bool GuardedProcessEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
	{
		__try {
			a_obj->ProcessEvent(a_fn, a_params);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// an object being destroyed or garbage: never a world context
	inline bool Dying(UE::UObject* a_o)
	{
		if (!a_o || !reflect::IsLive(a_o)) return true;
		constexpr std::int32_t kObject = 0x00008000 | 0x00010000 | 0x20000000 | 0x40000000;   // BeginDestroyed, FinishDestroyed, PendingKill, Garbage
		return (static_cast<std::int32_t>(a_o->objectFlags) & kObject) != 0;
	}

	// A reflected call: parameters by name, laid out from the UFunction's own properties.
	class Call
	{
	public:
		Call(UE::UObject* a_obj, const wchar_t* a_fn) :
			m_obj(a_obj),
			m_fn(a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr)
		{
			if (m_fn) {
				m_params.assign(static_cast<std::size_t>(reinterpret_cast<UE::UStruct*>(m_fn)->propertiesSize), 0);
			}
		}
		explicit operator bool() const { return m_fn != nullptr; }
		void* At(std::string_view a_name)
		{
			if (!m_fn) {
				return nullptr;
			}
			const auto off = reflect::Offset(reinterpret_cast<UE::UStruct*>(m_fn), a_name);
			return off >= 0 ? m_params.data() + off : nullptr;
		}
		template <class T>
		bool Set(std::string_view a_name, const T& a_value)
		{
			if (void* p = At(a_name)) {
				std::memcpy(p, &a_value, sizeof(T));
				return true;
			}
			return false;
		}
		bool SetBytes(std::string_view a_name, const void* a_src, std::size_t a_size)
		{
			if (void* p = At(a_name)) {
				std::memcpy(p, a_src, a_size);
				return true;
			}
			return false;
		}
		template <class T>
		T Get(std::string_view a_name)
		{
			T v{};
			if (void* p = At(a_name)) {
				std::memcpy(&v, p, sizeof(T));
			}
			return v;
		}
		bool Run()
		{
			if (!m_fn || !m_obj) {
				return false;
			}
			m_obj->ProcessEvent(m_fn, m_params.data());
			return true;
		}
		bool RunGuarded()
		{
			return m_fn && m_obj && GuardedProcessEvent(m_obj, m_fn, m_params.data());
		}

	private:
		UE::UObject*              m_obj;
		UE::UFunction*            m_fn;
		std::vector<std::uint8_t> m_params;
	};

	// the player controller's IsInputKeyDown - a key's state now, whatever device (safe to ask more than once a frame,
	// unlike WasInputKeyJustPressed)
	inline bool KeyDown(UE::UObject* a_pc, const UE::FName& a_key)
	{
		Call c(a_pc, L"IsInputKeyDown");
		void* k = c ? c.At("Key") : nullptr;
		if (!k) {
			return false;
		}
		new (k) UE::FKey(a_key);
		c.Run();
		static_cast<UE::FKey*>(k)->~FKey();   // the engine may attach its key details: released every call
		const bool* down = static_cast<const bool*>(c.At("ReturnValue"));
		return down && *down;
	}

	// the first live object whose class is a_base or derives from it (not a class default object)
	inline UE::UObject* FirstOf(UE::UClass* a_base)
	{
		auto* arr = UE::FUObjectArray::GetSingleton();
		if (!arr || !a_base) {
			return nullptr;
		}
		UE::UObject* found = nullptr;
		arr->LockInternalArray();
		const std::int32_t n = arr->GetObjectArrayNum();
		for (std::int32_t i = 0; i < n && !found; ++i) {
			auto* item = arr->IndexToObject(i);
			auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
			auto* cls = o ? o->GetClass() : nullptr;
			if (cls && cls->IsChildOf(a_base) && o != cls->GetDefaultObject(false)) {
				found = o;
			}
		}
		arr->UnlockInternalArray();
		return found;
	}
}
