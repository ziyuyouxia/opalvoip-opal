/* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 2.0.9
 *
 * Do not make changes to this file unless you know what you are doing--modify
 * the SWIG interface file instead.
 * ----------------------------------------------------------------------------- */


using System;
using System.Runtime.InteropServices;

public class OpalMessage : IDisposable {
  private HandleRef swigCPtr;
  protected bool swigCMemOwn;

  internal OpalMessage(IntPtr cPtr, bool cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = new HandleRef(this, cPtr);
  }

  internal static HandleRef getCPtr(OpalMessage obj) {
    return (obj == null) ? new HandleRef(null, IntPtr.Zero) : obj.swigCPtr;
  }

  ~OpalMessage() {
    Dispose();
  }

  public virtual void Dispose() {
    lock(this) {
      if (swigCPtr.Handle != IntPtr.Zero) {
        if (swigCMemOwn) {
          swigCMemOwn = false;
          OPALPINVOKE.delete_OpalMessage(swigCPtr);
        }
        swigCPtr = new HandleRef(null, IntPtr.Zero);
      }
      GC.SuppressFinalize(this);
    }
  }

  public OpalMessageType type {
    set {
      OPALPINVOKE.OpalMessage_type_set(swigCPtr, (int)value);
    } 
    get {
      OpalMessageType ret = (OpalMessageType)OPALPINVOKE.OpalMessage_type_get(swigCPtr);
      return ret;
    } 
  }

  public OpalMessageParam param {
    set {
      OPALPINVOKE.OpalMessage_param_set(swigCPtr, OpalMessageParam.getCPtr(value));
    } 
    get {
      IntPtr cPtr = OPALPINVOKE.OpalMessage_param_get(swigCPtr);
      OpalMessageParam ret = (cPtr == IntPtr.Zero) ? null : new OpalMessageParam(cPtr, false);
      return ret;
    } 
  }

  public OpalMessage() : this(OPALPINVOKE.new_OpalMessage(), true) {
  }

}