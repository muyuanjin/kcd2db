//
// Created by muyuanjin on 2025/3/24.
//

#ifndef CONSOLE_H
#define CONSOLE_H
#include <cstdint>
#include "SFunctor.h"


struct ICVar;
typedef void (*ConsoleVarFunc)(ICVar*);

struct ICVar
{
    enum EConsoleLogMode
    {
        eCLM_Off, //!< Off.
        eCLM_ConsoleAndFile, //!< Normal info to console and file.
        eCLM_FileOnly, //!< Normal info to file only.
        eCLM_FullInfo //!< Full info to file only.
    };

    virtual ~ICVar() = default;

    //! Delete the variable.
    //! \note The variable will automatically unregister itself from the console.
    virtual void Release() = 0;

    //! \return Value of the variable as an integer.
    virtual int GetIVal() const = 0;

    //! \return The 64-bit integer value of the variable.
    virtual int64_t GetI64Val() const = 0;

    //! \return The float value of the variable.
    virtual float GetFVal() const = 0;

    //! \note Don't store pointer as multiple calls to this function might return same memory ptr.
    //! \return The string value of the variable.
    virtual const char* GetString() const = 0;

    //! \note Don't store pointer as multiple calls to this function might return same memory ptr.
    //! \return The data probe string value of the variable.
    virtual const char* GetDataProbeString() const = 0;

    //! Set the string value of the variable.
    //! \param s String representation the value.
    virtual void Set(const char* s) = 0;

    //! Force to set the string value of the variable - can only be called from inside code.
    //! \param s String representation the value.
    virtual void ForceSet(const char* s) = 0;

    //! Set the float value of the variable.
    //! \param f Float representation the value.
    virtual void Set(float f) = 0;

    //! Set the float value of the variable.
    //! \param i integer representation the value.
    virtual void Set(int i) = 0;

    //! Clear the specified bits in the flag field.
    virtual void ClearFlags(int flags) = 0;

    //! Return the variable's flags.
    //! \return the variable's flags.
    virtual int GetFlags() const = 0;

    //! Set the variable's flags.
    virtual int SetFlags(int flags) = 0;

    //! \return the primary variable's type, e.g. CVAR_INT, CVAR_FLOAT, CVAR_STRING.
    virtual int GetType() = 0;

    //! \return The variable's name.
    virtual const char* GetName() const = 0;

    //! \return The variable's help text, can be NULL if no help is available.
    virtual const char* GetHelp() = 0;

    //! \return true if the variable may be modified in config files.
    virtual bool IsConstCVar() const = 0;

    //! Set a new on change function callback.
    //! \note Deprecated function. The functor should be preferred.
    virtual void SetOnChangeCallback(ConsoleVarFunc pChangeFunc) = 0;

    //! Adds a new on change functor to the list.
    //! It will add from index 1 on (0 is reserved).
    virtual uint64_t AddOnChangeFunctor(const SFunctor& pChangeFunctor) = 0;

    //!  \return The number of registered on change functos.
    virtual uint64_t GetNumberOfOnChangeFunctors() const = 0;

    //! Returns the number of registered on change functors.
    virtual const SFunctor& GetOnChangeFunctor(uint64_t nFunctorIndex) const = 0;

    //! Removes an on change functor.
    //! \return true if removal was successful.
    virtual bool RemoveOnChangeFunctor(uint64_t nElement) = 0;
    virtual bool RemoveOnChangeFunctor(const SFunctor& changeFunctor) = 0;

    //! Get the current callback function.
    virtual ConsoleVarFunc GetOnChangeCallback() const = 0;

    virtual void GetMemoryUsage(class ICrySizer* pSizer) const = 0;

    //! Only useful for CVarGroups, other types return GetIVal().
    //! CVarGroups set multiple other CVars and this function returns
    //! the integer value the CVarGroup should have, when looking at the controlled cvars.
    //! \return Value that would represent the state, -1 if the state cannot be found
    virtual int GetRealIVal() const = 0;

    //! Log difference between expected state and real state. Only useful for CVarGroups.
    virtual void DebugLog(const int iExpectedValue, const EConsoleLogMode mode) const
    {
    }
};

struct IConsole
{
    virtual ~IConsole() = default;
    virtual void Release() = 0;
    virtual ICVar* RegisterString(const char* sName, const char* sValue, int nFlags, const char* help = "", ConsoleVarFunc pChangeFunc = 0) = 0;

};
#endif //CONSOLE_H
