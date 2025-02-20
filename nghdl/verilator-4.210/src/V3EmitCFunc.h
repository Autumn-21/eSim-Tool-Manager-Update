// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Emit C++ for tree
//
// Code available from: https://verilator.org
//
//*************************************************************************
//
// Copyright 2003-2021 by Wilson Snyder. This program is free software; you
// can redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************

#ifndef VERILATOR_V3EMITCFUNC_H_
#define VERILATOR_V3EMITCFUNC_H_

#include "config_build.h"
#include "verilatedos.h"

#include "V3Global.h"
#include "V3EmitCBase.h"

#include <algorithm>
#include <map>
#include <unordered_set>
#include <vector>

// Number of VL_CONST_W_*X's in verilated.h (IE VL_CONST_W_8X is last)
constexpr int EMITC_NUM_CONSTW = 8;

//######################################################################
// Emit lazy forward declarations

class EmitCLazyDecls final : public AstNVisitor {
    // NODE STATE/TYPES
    //  AstNode::user2() -> bool. Already emitted decl for symbols.
    AstUser2InUse m_inuser2;

    // MEMBERS
    std::unordered_set<string> m_emittedManually;  // Set of names already declared manually.
    EmitCBaseVisitor& m_emitter;  // For access to file output
    bool m_needsBlankLine = false;  // Emit blank line if any declarations were emitted (cosmetic)

    // METHODS
    void lazyDeclare(AstCFunc* funcp) {
        // Already declared in this compilation unit
        if (funcp->user2SetOnce()) return;
        // Check if this kind of function is lazily declared
        if (!(funcp->isMethod() && funcp->isLoose()) && !funcp->dpiImportPrototype()) return;
        // Already declared manually
        if (m_emittedManually.count(funcp->nameProtect())) return;
        // Needs lazy declaration, emit one
        m_emitter.emitCFuncDecl(funcp, VN_CAST_CONST(funcp->user4p(), NodeModule),
                                funcp->dpiImportPrototype());
        m_needsBlankLine = true;
    }

    void lazyDeclareConstPoolVar(AstVar* varp) {
        if (varp->user2SetOnce()) return;  // Already declared
        const string nameProtect
            = m_emitter.topClassName() + "__ConstPool__" + varp->nameProtect();
        m_emitter.puts("extern const ");
        m_emitter.puts(varp->dtypep()->cType(nameProtect, false, false));
        m_emitter.puts(";\n");
        m_needsBlankLine = true;
    }

    // VISITORS
    virtual void visit(AstNodeCCall* nodep) override {
        lazyDeclare(nodep->funcp());
        iterateChildren(nodep);
    }

    virtual void visit(AstAddrOfCFunc* nodep) override {
        lazyDeclare(nodep->funcp());
        iterateChildren(nodep);
    }

    virtual void visit(AstVarRef* nodep) override {
        AstVar* const varp = nodep->varp();
        // Only constant pool symbols are lazy declared for now ...
        if (EmitCBaseVisitor::isConstPoolMod(varp->user4p())) { lazyDeclareConstPoolVar(varp); }
    }

    virtual void visit(AstNode* nodep) override { iterateChildrenConst(nodep); }

    VL_DEBUG_FUNC;

public:
    EmitCLazyDecls(EmitCBaseVisitor& emitter)
        : m_emitter(emitter) {}
    void emit(AstNode* nodep) {
        m_needsBlankLine = false;
        iterateChildrenConst(nodep);
        if (m_needsBlankLine) m_emitter.puts("\n");
    }
    void emit(const string& prefix, const string& name, const string& suffix) {
        m_emittedManually.insert(name);
        m_emitter.ensureNewLine();
        m_emitter.puts(prefix);
        m_emitter.puts(name);
        m_emitter.puts(suffix);
        m_emitter.ensureNewLine();
    }
    void declared(AstCFunc* nodep) { nodep->user2SetOnce(); }
    void reset() { AstNode::user2ClearTree(); }
};

//######################################################################
// Emit statements and math operators

class EmitCFunc VL_NOT_FINAL : public EmitCBaseVisitor {
private:
    using VarVec = std::vector<const AstVar*>;
    using VarSortMap = std::map<int, VarVec>;  // Map size class to VarVec

    bool m_suppressSemi;
    AstVarRef* m_wideTempRefp;  // Variable that _WW macros should be setting
    VarVec m_ctorVarsVec;  // All variables in constructor order
    int m_labelNum;  // Next label number
    int m_splitSize;  // # of cfunc nodes placed into output file
    int m_splitFilenum;  // File number being created, 0 = primary
    bool m_inUC = false;  // Inside an AstUCStmt or AstUCMath
    std::vector<AstChangeDet*> m_blkChangeDetVec;  // All encountered changes in block

protected:
    EmitCLazyDecls m_lazyDecls;  // Visitor for emitting lazy declarations
    bool m_useSelfForThis = false;  // Replace "this" with "vlSelf"
    AstNodeModule* m_modp = nullptr;  // Current module being emitted

public:
    // METHODS
    VL_DEBUG_FUNC;  // Declare debug()

    // ACCESSORS
    int splitFilenumInc() {
        m_splitSize = 0;
        return ++m_splitFilenum;
    }
    int splitSize() const { return m_splitSize; }
    void splitSizeInc(int count) { m_splitSize += count; }
    void splitSizeInc(AstNode* nodep) { splitSizeInc(EmitCBaseCounterVisitor(nodep).count()); }
    bool splitNeeded() const {
        return v3Global.opt.outputSplit() && splitSize() >= v3Global.opt.outputSplit();
    }

    // METHODS
    void displayNode(AstNode* nodep, AstScopeName* scopenamep, const string& vformat,
                     AstNode* exprsp, bool isScan);
    void displayEmit(AstNode* nodep, bool isScan);
    void displayArg(AstNode* dispp, AstNode** elistp, bool isScan, const string& vfmt, bool ignore,
                    char fmtLetter);

    enum EisWhich : uint8_t {
        EVL_CLASS_IO,
        EVL_CLASS_SIG,
        EVL_CLASS_TEMP,
        EVL_CLASS_PAR,
        EVL_CLASS_ALL,
        EVL_FUNC_ALL
    };
    void emitVarList(AstNode* firstp, EisWhich which, const string& prefixIfImp, string& sectionr);
    static void emitVarSort(const VarSortMap& vmap, VarVec* sortedp);
    void emitSortedVarList(const VarVec& anons, const VarVec& nonanons, const string& prefixIfImp);
    void emitVarCtors(bool* firstp);
    void emitCtorSep(bool* firstp);
    bool emitSimpleOk(AstNodeMath* nodep);
    void emitIQW(AstNode* nodep) {
        // Other abbrevs: "C"har, "S"hort, "F"loat, "D"ouble, stri"N"g
        puts(nodep->dtypep()->charIQWN());
    }
    void emitScIQW(AstVar* nodep) {
        UASSERT_OBJ(nodep->isSc(), nodep, "emitting SystemC operator on non-SC variable");
        // clang-format off
        puts(nodep->isScBigUint() ? "SB"
             : nodep->isScUint()  ? "SU"
             : nodep->isScBv()    ? "SW"
             : (nodep->isScQuad() ? "SQ" : "SI"));
        // clang-format on
    }
    void emitDatap(AstNode* nodep) {
        // When passing to a function with va_args the compiler doesn't
        // know need a pointer so when wide, need to look inside VlWide
        if (nodep->isWide()) puts(".data()");
    }
    void emitOpName(AstNode* nodep, const string& format, AstNode* lhsp, AstNode* rhsp,
                    AstNode* thsp);
    void emitCCallArgs(AstNodeCCall* nodep);
    void emitDereference(const string& pointer);
    void emitCvtPackStr(AstNode* nodep);
    void emitCvtWideArray(AstNode* nodep, AstNode* fromp);
    void emitConstant(AstConst* nodep, AstVarRef* assigntop, const string& assignString);
    void emitSetVarConstant(const string& assignString, AstConst* constp);
    void emitVarReset(AstVar* varp);
    string emitVarResetRecurse(const AstVar* varp, const string& varNameProtected,
                               AstNodeDType* dtypep, int depth, const string& suffix);
    void doubleOrDetect(AstChangeDet* changep, bool& gotOne);
    void emitChangeDet();

    // VISITORS
    virtual void visit(AstCFunc* nodep) override {
        VL_RESTORER(m_useSelfForThis);

        m_blkChangeDetVec.clear();

        splitSizeInc(nodep);

        puts("\n");
        m_lazyDecls.emit(nodep);
        if (nodep->ifdef() != "") puts("#ifdef " + nodep->ifdef() + "\n");
        if (nodep->isInline()) puts("VL_INLINE_OPT ");
        emitCFuncHeader(nodep, m_modp, /* withScope: */ true);

        // TODO perhaps better to have a new AstCCtorInit so we can pass arguments
        // rather than requiring a string here
        if (!nodep->ctorInits().empty()) {
            puts(": ");
            puts(nodep->ctorInits());
        }
        puts(" {\n");

        if (nodep->isLoose()) {
            m_lazyDecls.declared(nodep);  // Defined here, so no longer needs declaration
            if (!nodep->isStatic()) {  // Standard prologue
                m_useSelfForThis = true;
                puts("if (false && vlSelf) {}  // Prevent unused\n");
                if (!VN_IS(m_modp, Class)) puts(symClassAssign());
            }
        }

        // "+" in the debug indicates a print from the model
        puts("VL_DEBUG_IF(VL_DBG_MSGF(\"+  ");
        for (int i = 0; i < m_modp->level(); ++i) { puts("  "); }
        puts(prefixNameProtect(m_modp));
        puts(nodep->isLoose() ? "__" : "::");
        puts(nodep->nameProtect() + "\\n\"); );\n");

        if (nodep->initsp()) putsDecoration("// Variables\n");
        for (AstNode* subnodep = nodep->argsp(); subnodep; subnodep = subnodep->nextp()) {
            if (AstVar* varp = VN_CAST(subnodep, Var)) {
                if (varp->isFuncReturn()) emitVarDecl(varp, "");
            }
        }
        string section;
        emitVarList(nodep->initsp(), EVL_FUNC_ALL, "", section /*ref*/);
        emitVarList(nodep->stmtsp(), EVL_FUNC_ALL, "", section /*ref*/);

        iterateAndNextNull(nodep->initsp());

        if (nodep->stmtsp()) putsDecoration("// Body\n");
        iterateAndNextNull(nodep->stmtsp());
        if (!m_blkChangeDetVec.empty()) emitChangeDet();

        if (nodep->finalsp()) putsDecoration("// Final\n");
        iterateAndNextNull(nodep->finalsp());
        //

        if (!m_blkChangeDetVec.empty()) puts("return __req;\n");

        puts("}\n");
        if (nodep->ifdef() != "") puts("#endif  // " + nodep->ifdef() + "\n");
    }

    virtual void visit(AstNodeAssign* nodep) override {
        bool paren = true;
        bool decind = false;
        if (AstSel* selp = VN_CAST(nodep->lhsp(), Sel)) {
            if (selp->widthMin() == 1) {
                putbs("VL_ASSIGNBIT_");
                emitIQW(selp->fromp());
                if (nodep->rhsp()->isAllOnesV()) {
                    puts("O(");
                } else {
                    puts("I(");
                }
                puts(cvtToStr(nodep->widthMin()) + ",");
                iterateAndNextNull(selp->lsbp());
                puts(", ");
                iterateAndNextNull(selp->fromp());
                puts(", ");
            } else {
                putbs("VL_ASSIGNSEL_");
                emitIQW(selp->fromp());
                puts("II");
                emitIQW(nodep->rhsp());
                puts("(");
                puts(cvtToStr(selp->fromp()->widthMin()) + ",");
                puts(cvtToStr(nodep->widthMin()) + ",");
                iterateAndNextNull(selp->lsbp());
                puts(", ");
                iterateAndNextNull(selp->fromp());
                puts(", ");
            }
        } else if (AstGetcRefN* selp = VN_CAST(nodep->lhsp(), GetcRefN)) {
            iterateAndNextNull(selp->lhsp());
            puts(" = ");
            putbs("VL_PUTC_N(");
            iterateAndNextNull(selp->lhsp());
            puts(", ");
            iterateAndNextNull(selp->rhsp());
            puts(", ");
        } else if (AstVar* varp = AstVar::scVarRecurse(nodep->lhsp())) {
            putbs("VL_ASSIGN_");  // Set a systemC variable
            emitScIQW(varp);
            emitIQW(nodep);
            puts("(");
            puts(cvtToStr(nodep->widthMin()) + ",");
            iterateAndNextNull(nodep->lhsp());
            puts(", ");
        } else if (AstVar* varp = AstVar::scVarRecurse(nodep->rhsp())) {
            putbs("VL_ASSIGN_");  // Get a systemC variable
            emitIQW(nodep);
            emitScIQW(varp);
            puts("(");
            puts(cvtToStr(nodep->widthMin()) + ",");
            iterateAndNextNull(nodep->lhsp());
            puts(", ");
        } else if (nodep->isWide() && VN_IS(nodep->lhsp(), VarRef)  //
                   && !VN_IS(nodep->rhsp(), CMath)  //
                   && !VN_IS(nodep->rhsp(), CMethodHard)  //
                   && !VN_IS(nodep->rhsp(), VarRef)  //
                   && !VN_IS(nodep->rhsp(), AssocSel)  //
                   && !VN_IS(nodep->rhsp(), ArraySel)) {
            // Wide functions assign into the array directly, don't need separate assign statement
            m_wideTempRefp = VN_CAST(nodep->lhsp(), VarRef);
            paren = false;
        } else if (nodep->isWide()) {
            putbs("VL_ASSIGN_W(");
            puts(cvtToStr(nodep->widthMin()) + ",");
            iterateAndNextNull(nodep->lhsp());
            puts(", ");
        } else {
            paren = false;
            iterateAndNextNull(nodep->lhsp());
            puts(" ");
            ofp()->blockInc();
            decind = true;
            if (!VN_IS(nodep->rhsp(), Const)) ofp()->putBreak();
            puts("= ");
        }
        iterateAndNextNull(nodep->rhsp());
        if (paren) puts(")");
        if (decind) ofp()->blockDec();
        if (!m_suppressSemi) puts(";\n");
    }
    virtual void visit(AstAlwaysPublic*) override {}
    virtual void visit(AstAssocSel* nodep) override {
        iterateAndNextNull(nodep->fromp());
        putbs(".at(");
        AstAssocArrayDType* adtypep = VN_CAST(nodep->fromp()->dtypep(), AssocArrayDType);
        UASSERT_OBJ(adtypep, nodep, "Associative select on non-associative type");
        if (adtypep->keyDTypep()->isWide()) {
            emitCvtWideArray(nodep->bitp(), nodep->fromp());
        } else {
            iterateAndNextNull(nodep->bitp());
        }
        puts(")");
    }
    virtual void visit(AstNodeCCall* nodep) override {
        const AstCFunc* const funcp = nodep->funcp();
        if (AstCMethodCall* ccallp = VN_CAST(nodep, CMethodCall)) {
            UASSERT_OBJ(!funcp->isLoose(), nodep, "Loose method called via AstCMethodCall");
            // make this a Ast type for future opt
            iterate(ccallp->fromp());
            putbs("->");
            puts(funcp->nameProtect());
        } else if (funcp->dpiImportPrototype()) {
            // Calling DPI import
            puts(funcp->name());
        } else if (funcp->isProperMethod() && funcp->isStatic()) {
            // Call static method via the containing class
            puts(prefixNameProtect(funcp->user4p()) + "::");
            puts(funcp->nameProtect());
        } else if (VN_IS(funcp->user4p(), Class) && funcp->user4p() != m_modp) {
            // Calling superclass method
            puts(prefixNameProtect(funcp->user4p()) + "::");
            puts(funcp->nameProtect());
        } else if (funcp->isLoose()) {
            // Calling loose method
            puts(funcNameProtect(funcp));
        } else {
            // Calling regular method/function
            if (!nodep->selfPointer().empty()) {
                emitDereference(nodep->selfPointerProtect(m_useSelfForThis));
            }
            puts(funcp->nameProtect());
        }
        puts("(");
        emitCCallArgs(nodep);
        if (VN_IS(nodep->backp(), NodeMath) || VN_IS(nodep->backp(), CReturn)) {
            // We should have a separate CCall for math and statement usage, but...
            puts(")");
        } else {
            puts(");\n");
        }
    }
    virtual void visit(AstCMethodHard* nodep) override {
        iterate(nodep->fromp());
        puts(".");
        puts(nodep->nameProtect());
        puts("(");
        bool comma = false;
        for (AstNode* subnodep = nodep->pinsp(); subnodep; subnodep = subnodep->nextp()) {
            if (comma) puts(", ");
            // handle wide arguments to the queues
            if (VN_IS(nodep->fromp()->dtypep(), QueueDType) && subnodep->dtypep()->isWide()) {
                emitCvtWideArray(subnodep, nodep->fromp());
            } else {
                iterate(subnodep);
            }
            comma = true;
        }
        puts(")");
        // Some are statements some are math.
        if (nodep->isStatement()) puts(";\n");
        UASSERT_OBJ(!nodep->isStatement() || VN_IS(nodep->dtypep(), VoidDType), nodep,
                    "Statement of non-void data type");
    }
    virtual void visit(AstLambdaArgRef* nodep) override { putbs(nodep->nameProtect()); }
    virtual void visit(AstWith* nodep) override {
        // With uses a C++11 lambda
        putbs("[=](");
        if (auto* argrefp = nodep->indexArgRefp()) {
            putbs(argrefp->dtypep()->cType(argrefp->nameProtect(), false, false));
            puts(",");
        }
        if (auto* argrefp = nodep->valueArgRefp()) {
            putbs(argrefp->dtypep()->cType(argrefp->nameProtect(), false, false));
        }
        // Probably fragile, V3Task may need to convert to a AstCReturn
        puts(") { return ");
        iterateAndNextNull(nodep->exprp());
        puts("; }\n");
    }
    virtual void visit(AstIntfRef* nodep) override {
        putsQuoted(VIdProtect::protectWordsIf(AstNode::vcdName(nodep->name()), nodep->protect()));
    }
    virtual void visit(AstNodeCase* nodep) override {  // LCOV_EXCL_LINE
        // In V3Case...
        nodep->v3fatalSrc("Case statements should have been reduced out");
    }
    virtual void visit(AstComment* nodep) override {
        string at;
        if (nodep->showAt()) {
            at = " at " + nodep->fileline()->ascii();
            // If protecting, passthru less information about the design
            if (!v3Global.opt.protectIds()) return;
        }
        if (!(nodep->protect() && v3Global.opt.protectIds())) {
            putsDecoration(string("// ") + nodep->name() + at + "\n");
        }
        iterateChildren(nodep);
    }
    virtual void visit(AstCoverDecl* nodep) override {
        puts("vlSelf->__vlCoverInsert(");  // As Declared in emitCoverageDecl
        puts("&(vlSymsp->__Vcoverage[");
        puts(cvtToStr(nodep->dataDeclThisp()->binNum()));
        puts("])");
        // If this isn't the first instantiation of this module under this
        // design, don't really count the bucket, and rely on verilator_cov to
        // aggregate counts.  This is because Verilator combines all
        // hierarchies itself, and if verilator_cov also did it, you'd end up
        // with (number-of-instant) times too many counts in this bin.
        puts(", first");  // Enable, passed from __Vconfigure parameter
        puts(", ");
        putsQuoted(protect(nodep->fileline()->filename()));
        puts(", ");
        puts(cvtToStr(nodep->fileline()->lineno()));
        puts(", ");
        puts(cvtToStr(nodep->offset() + nodep->fileline()->firstColumn()));
        puts(", ");
        putsQuoted((!nodep->hier().empty() ? "." : "")
                   + protectWordsIf(nodep->hier(), nodep->protect()));
        puts(", ");
        putsQuoted(protectWordsIf(nodep->page(), nodep->protect()));
        puts(", ");
        putsQuoted(protectWordsIf(nodep->comment(), nodep->protect()));
        puts(", ");
        putsQuoted(nodep->linescov());
        puts(");\n");
    }
    virtual void visit(AstCoverInc* nodep) override {
        if (v3Global.opt.threads()) {
            puts("vlSymsp->__Vcoverage[");
            puts(cvtToStr(nodep->declp()->dataDeclThisp()->binNum()));
            puts("].fetch_add(1, std::memory_order_relaxed);\n");
        } else {
            puts("++(vlSymsp->__Vcoverage[");
            puts(cvtToStr(nodep->declp()->dataDeclThisp()->binNum()));
            puts("]);\n");
        }
    }
    virtual void visit(AstCReturn* nodep) override {
        puts("return (");
        iterateAndNextNull(nodep->lhsp());
        puts(");\n");
    }
    virtual void visit(AstDisplay* nodep) override {
        string text = nodep->fmtp()->text();
        if (nodep->addNewline()) text += "\n";
        displayNode(nodep, nodep->fmtp()->scopeNamep(), text, nodep->fmtp()->exprsp(), false);
    }
    virtual void visit(AstDumpCtl* nodep) override {
        switch (nodep->ctlType()) {
        case VDumpCtlType::FILE:
            puts("vlSymsp->_vm_contextp__->dumpfile(");
            emitCvtPackStr(nodep->exprp());
            puts(");\n");
            break;
        case VDumpCtlType::VARS:
            // We ignore number of levels to dump in exprp()
            if (v3Global.opt.trace()) {
                puts("vlSymsp->_traceDumpOpen();\n");
            } else {
                puts("VL_PRINTF_MT(\"-Info: ");
                puts(protect(nodep->fileline()->filename()));
                puts(":");
                puts(cvtToStr(nodep->fileline()->lineno()));
                puts(": $dumpvar ignored, as Verilated without --trace");
                puts("\\n\");\n");
            }
            break;
        case VDumpCtlType::ALL:
            // $dumpall currently ignored
            break;
        case VDumpCtlType::FLUSH:
            // $dumpall currently ignored; would need rework of VCD single thread,
            // or flag we pass-through to next eval() iteration
            break;
        case VDumpCtlType::LIMIT:
            // $dumplimit currently ignored
            break;
        case VDumpCtlType::OFF:
            // Currently ignored as both Vcd and Fst do not support them, as would need "X" dump
            break;
        case VDumpCtlType::ON:
            // Currently ignored as $dumpoff is also ignored
            break;
        default: nodep->v3fatalSrc("Bad case, unexpected " << nodep->ctlType().ascii());
        }
    }
    virtual void visit(AstScopeName* nodep) override {
        // For use under AstCCalls for dpiImports.  ScopeNames under
        // displays are handled in AstDisplay
        if (!nodep->dpiExport()) {
            // this is where the DPI import context scope is set
            const string scope = nodep->scopeDpiName();
            putbs("(&(vlSymsp->" + protect("__Vscope_" + scope) + "))");
        }
    }
    virtual void visit(AstSFormat* nodep) override {
        displayNode(nodep, nodep->fmtp()->scopeNamep(), nodep->fmtp()->text(),
                    nodep->fmtp()->exprsp(), false);
    }
    virtual void visit(AstSFormatF* nodep) override {
        displayNode(nodep, nodep->scopeNamep(), nodep->text(), nodep->exprsp(), false);
    }
    virtual void visit(AstFScanF* nodep) override {
        displayNode(nodep, nullptr, nodep->text(), nodep->exprsp(), true);
    }
    virtual void visit(AstSScanF* nodep) override {
        displayNode(nodep, nullptr, nodep->text(), nodep->exprsp(), true);
    }
    virtual void visit(AstValuePlusArgs* nodep) override {
        puts("VL_VALUEPLUSARGS_IN");
        emitIQW(nodep->outp());
        puts("(");
        puts(cvtToStr(nodep->outp()->widthMin()));
        puts(", ");
        emitCvtPackStr(nodep->searchp());
        puts(", ");
        putbs("");
        iterateAndNextNull(nodep->outp());
        puts(")");
    }
    virtual void visit(AstTestPlusArgs* nodep) override {
        puts("VL_TESTPLUSARGS_I(");
        putsQuoted(nodep->text());
        puts(")");
    }
    virtual void visit(AstFError* nodep) override {
        puts("VL_FERROR_IN(");
        iterateAndNextNull(nodep->filep());
        putbs(", ");
        iterateAndNextNull(nodep->strp());
        puts(")");
    }
    virtual void visit(AstFGetS* nodep) override {
        checkMaxWords(nodep);
        emitOpName(nodep, nodep->emitC(), nodep->lhsp(), nodep->rhsp(), nullptr);
    }

    void checkMaxWords(AstNode* nodep) {
        if (nodep->widthWords() > VL_TO_STRING_MAX_WORDS) {
            nodep->v3error(
                "String of "
                << nodep->width()
                << " bits exceeds hardcoded limit VL_TO_STRING_MAX_WORDS in verilatedos.h");
        }
    }
    virtual void visit(AstFOpen* nodep) override {
        iterateAndNextNull(nodep->filep());
        puts(" = VL_FOPEN_NN(");
        emitCvtPackStr(nodep->filenamep());
        putbs(", ");
        if (nodep->modep()->width() > 4 * 8)
            nodep->modep()->v3error("$fopen mode should be <= 4 characters");
        emitCvtPackStr(nodep->modep());
        puts(");\n");
    }
    virtual void visit(AstFOpenMcd* nodep) override {
        iterateAndNextNull(nodep->filep());
        puts(" = VL_FOPEN_MCD_N(");
        emitCvtPackStr(nodep->filenamep());
        puts(");\n");
    }
    virtual void visit(AstNodeReadWriteMem* nodep) override {
        puts(nodep->cFuncPrefixp());
        puts("N(");
        puts(nodep->isHex() ? "true" : "false");
        putbs(", ");
        // Need real storage width
        puts(cvtToStr(nodep->memp()->dtypep()->subDTypep()->widthMin()));
        uint32_t array_lo = 0;
        {
            const AstVarRef* varrefp = VN_CAST(nodep->memp(), VarRef);
            if (!varrefp) {
                nodep->v3error(nodep->verilogKwd() << " loading non-variable");
            } else if (VN_IS(varrefp->varp()->dtypeSkipRefp(), AssocArrayDType)) {
                // nodep->memp() below will when verilated code is compiled create a C++ template
            } else if (const AstUnpackArrayDType* adtypep
                       = VN_CAST(varrefp->varp()->dtypeSkipRefp(), UnpackArrayDType)) {
                putbs(", ");
                puts(cvtToStr(varrefp->varp()->dtypep()->arrayUnpackedElements()));
                array_lo = adtypep->lo();
                putbs(", ");
                puts(cvtToStr(array_lo));
            } else {
                nodep->v3error(nodep->verilogKwd()
                               << " loading other than unpacked/associative-array variable");
            }
        }
        putbs(", ");
        emitCvtPackStr(nodep->filenamep());
        putbs(", ");
        {
            const bool need_ptr = !VN_IS(nodep->memp()->dtypep(), AssocArrayDType);
            if (need_ptr) puts(" &(");
            iterateAndNextNull(nodep->memp());
            if (need_ptr) puts(")");
        }
        putbs(", ");
        if (nodep->lsbp()) {
            iterateAndNextNull(nodep->lsbp());
        } else {
            puts(cvtToStr(array_lo));
        }
        putbs(", ");
        if (nodep->msbp()) {
            iterateAndNextNull(nodep->msbp());
        } else {
            puts("~0ULL");
        }
        puts(");\n");
    }
    virtual void visit(AstFClose* nodep) override {
        puts("VL_FCLOSE_I(");
        iterateAndNextNull(nodep->filep());
        puts("); ");
        iterateAndNextNull(nodep->filep());  // For safety, so user doesn't later WRITE with it.
        puts(" = 0;\n");
    }
    virtual void visit(AstFFlush* nodep) override {
        if (!nodep->filep()) {
            puts("Verilated::runFlushCallbacks();\n");
        } else {
            puts("if (");
            iterateAndNextNull(nodep->filep());
            puts(") { VL_FFLUSH_I(");
            iterateAndNextNull(nodep->filep());
            puts("); }\n");
        }
    }
    virtual void visit(AstFSeek* nodep) override {
        puts("(VL_FSEEK_I(");
        iterateAndNextNull(nodep->filep());
        puts(",");
        iterateAndNextNull(nodep->offset());
        puts(",");
        iterateAndNextNull(nodep->operation());
        puts(") == -1 ? -1 : 0)");
    }
    virtual void visit(AstFTell* nodep) override {
        puts("VL_FTELL_I(");
        iterateAndNextNull(nodep->filep());
        puts(")");
    }
    virtual void visit(AstFRewind* nodep) override {
        puts("(VL_FSEEK_I(");
        iterateAndNextNull(nodep->filep());
        puts(", 0, 0) == -1 ? -1 : 0)");
    }
    virtual void visit(AstFRead* nodep) override {
        puts("VL_FREAD_I(");
        puts(cvtToStr(nodep->memp()->widthMin()));  // Need real storage width
        putbs(",");
        uint32_t array_lo = 0;
        uint32_t array_size = 0;
        {
            const AstVarRef* varrefp = VN_CAST(nodep->memp(), VarRef);
            if (!varrefp) {
                nodep->v3error(nodep->verilogKwd() << " loading non-variable");
            } else if (VN_CAST(varrefp->varp()->dtypeSkipRefp(), BasicDType)) {
            } else if (const AstUnpackArrayDType* adtypep
                       = VN_CAST(varrefp->varp()->dtypeSkipRefp(), UnpackArrayDType)) {
                array_lo = adtypep->lo();
                array_size = adtypep->elementsConst();
            } else {
                nodep->v3error(nodep->verilogKwd()
                               << " loading other than unpacked-array variable");
            }
        }
        puts(cvtToStr(array_lo));
        putbs(",");
        puts(cvtToStr(array_size));
        putbs(", ");
        puts("&(");
        iterateAndNextNull(nodep->memp());
        puts(")");
        putbs(", ");
        iterateAndNextNull(nodep->filep());
        putbs(", ");
        if (nodep->startp()) {
            iterateAndNextNull(nodep->startp());
        } else {
            puts(cvtToStr(array_lo));
        }
        putbs(", ");
        if (nodep->countp()) {
            iterateAndNextNull(nodep->countp());
        } else {
            puts(cvtToStr(array_size));
        }
        puts(")");
    }
    virtual void visit(AstSysFuncAsTask* nodep) override {
        if (!nodep->lhsp()->isWide()) puts("(void)");
        iterateAndNextNull(nodep->lhsp());
        if (!nodep->lhsp()->isWide()) puts(";");
    }
    virtual void visit(AstSystemT* nodep) override {
        puts("(void)VL_SYSTEM_I");
        emitIQW(nodep->lhsp());
        puts("(");
        if (nodep->lhsp()->isWide()) {
            puts(cvtToStr(nodep->lhsp()->widthWords()));
            putbs(", ");
        }
        checkMaxWords(nodep->lhsp());
        iterateAndNextNull(nodep->lhsp());
        puts(");\n");
    }
    virtual void visit(AstSystemF* nodep) override {
        puts("VL_SYSTEM_I");
        emitIQW(nodep->lhsp());
        puts("(");
        if (nodep->lhsp()->isWide()) {
            puts(cvtToStr(nodep->lhsp()->widthWords()));
            putbs(", ");
        }
        checkMaxWords(nodep->lhsp());
        iterateAndNextNull(nodep->lhsp());
        puts(")");
    }
    virtual void visit(AstJumpBlock* nodep) override {
        nodep->labelNum(++m_labelNum);
        puts("{\n");  // Make it visually obvious label jumps outside these
        iterateAndNextNull(nodep->stmtsp());
        iterateAndNextNull(nodep->endStmtsp());
        puts("}\n");
    }
    virtual void visit(AstJumpGo* nodep) override {
        puts("goto __Vlabel" + cvtToStr(nodep->labelp()->blockp()->labelNum()) + ";\n");
    }
    virtual void visit(AstJumpLabel* nodep) override {
        puts("__Vlabel" + cvtToStr(nodep->blockp()->labelNum()) + ": ;\n");
    }
    virtual void visit(AstWhile* nodep) override {
        iterateAndNextNull(nodep->precondsp());
        puts("while (");
        iterateAndNextNull(nodep->condp());
        puts(") {\n");
        iterateAndNextNull(nodep->bodysp());
        iterateAndNextNull(nodep->incsp());
        iterateAndNextNull(nodep->precondsp());  // Need to recompute before next loop
        puts("}\n");
    }
    virtual void visit(AstNodeIf* nodep) override {
        puts("if (");
        if (!nodep->branchPred().unknown()) {
            puts(nodep->branchPred().ascii());
            puts("(");
        }
        iterateAndNextNull(nodep->condp());
        if (!nodep->branchPred().unknown()) puts(")");
        puts(") {\n");
        iterateAndNextNull(nodep->ifsp());
        puts("}");
        if (!nodep->elsesp()) {
            puts("\n");
        } else {
            if (VN_IS(nodep->elsesp(), NodeIf) && !nodep->elsesp()->nextp()) {
                puts(" else ");
                iterateAndNextNull(nodep->elsesp());
            } else {
                puts(" else {\n");
                iterateAndNextNull(nodep->elsesp());
                puts("}\n");
            }
        }
    }
    virtual void visit(AstExprStmt* nodep) override {
        // GCC allows compound statements in expressions, but this is not standard.
        // So we use an immediate-evaluation lambda and comma operator
        putbs("([&]() {\n");
        iterateAndNextNull(nodep->stmtsp());
        puts("}(), ");
        iterateAndNextNull(nodep->resultp());
        puts(")");
    }
    virtual void visit(AstStop* nodep) override {
        puts("VL_STOP_MT(");
        putsQuoted(protect(nodep->fileline()->filename()));
        puts(", ");
        puts(cvtToStr(nodep->fileline()->lineno()));
        puts(", \"\"");
        puts(");\n");
    }
    virtual void visit(AstFinish* nodep) override {
        puts("VL_FINISH_MT(");
        putsQuoted(protect(nodep->fileline()->filename()));
        puts(", ");
        puts(cvtToStr(nodep->fileline()->lineno()));
        puts(", \"\");\n");
    }
    virtual void visit(AstPrintTimeScale* nodep) override {
        puts("VL_PRINTTIMESCALE(");
        putsQuoted(protect(nodep->name()));
        puts(", ");
        putsQuoted(nodep->timeunit().ascii());
        puts(", vlSymsp->_vm_contextp__);\n");
    }
    virtual void visit(AstRand* nodep) override {
        emitOpName(nodep, nodep->emitC(), nodep->seedp(), nullptr, nullptr);
    }
    virtual void visit(AstTime* nodep) override {
        puts("VL_TIME_UNITED_Q(");
        if (nodep->timeunit().isNone()) nodep->v3fatalSrc("$time has no units");
        puts(cvtToStr(nodep->timeunit().multiplier()
                      / v3Global.rootp()->timeprecision().multiplier()));
        puts(")");
    }
    virtual void visit(AstTimeD* nodep) override {
        puts("VL_TIME_UNITED_D(");
        if (nodep->timeunit().isNone()) nodep->v3fatalSrc("$realtime has no units");
        puts(cvtToStr(nodep->timeunit().multiplier()
                      / v3Global.rootp()->timeprecision().multiplier()));
        puts(")");
    }
    virtual void visit(AstTimeFormat* nodep) override {
        puts("VL_TIMEFORMAT_IINI(");
        iterateAndNextNull(nodep->unitsp());
        puts(", ");
        iterateAndNextNull(nodep->precisionp());
        puts(", ");
        emitCvtPackStr(nodep->suffixp());
        puts(", ");
        iterateAndNextNull(nodep->widthp());
        puts(", vlSymsp->_vm_contextp__);\n");
    }
    virtual void visit(AstNodeSimpleText* nodep) override {
        const string text = m_inUC && m_useSelfForThis
                                ? VString::replaceWord(nodep->text(), "this", "vlSelf")
                                : nodep->text();
        if (nodep->tracking() || m_trackText) {
            puts(text);
        } else {
            ofp()->putsNoTracking(text);
        }
    }
    virtual void visit(AstTextBlock* nodep) override {
        visit(VN_CAST(nodep, NodeSimpleText));
        for (AstNode* childp = nodep->nodesp(); childp; childp = childp->nextp()) {
            iterate(childp);
            if (nodep->commas() && childp->nextp()) puts(", ");
        }
    }
    virtual void visit(AstCStmt* nodep) override {
        putbs("");
        iterateAndNextNull(nodep->bodysp());
    }
    virtual void visit(AstCMath* nodep) override {
        putbs("");
        iterateAndNextNull(nodep->bodysp());
    }
    virtual void visit(AstUCStmt* nodep) override {
        VL_RESTORER(m_inUC);
        m_inUC = true;
        putsDecoration(ifNoProtect("// $c statement at " + nodep->fileline()->ascii() + "\n"));
        iterateAndNextNull(nodep->bodysp());
        puts("\n");
    }
    virtual void visit(AstUCFunc* nodep) override {
        VL_RESTORER(m_inUC);
        m_inUC = true;
        puts("\n");
        putsDecoration(ifNoProtect("// $c function at " + nodep->fileline()->ascii() + "\n"));
        iterateAndNextNull(nodep->bodysp());
        puts("\n");
    }

    // Operators
    virtual void visit(AstNodeTermop* nodep) override {
        emitOpName(nodep, nodep->emitC(), nullptr, nullptr, nullptr);
    }
    virtual void visit(AstNodeUniop* nodep) override {
        if (nodep->emitCheckMaxWords()
            && (nodep->widthWords() > VL_MULS_MAX_WORDS
                || nodep->lhsp()->widthWords() > VL_MULS_MAX_WORDS)) {
            nodep->v3warn(
                E_UNSUPPORTED,
                "Unsupported: "
                    << nodep->prettyOperatorName() << " operator of " << nodep->width()
                    << " bits exceeds hardcoded limit VL_MULS_MAX_WORDS in verilatedos.h");
        }
        if (emitSimpleOk(nodep)) {
            putbs("(");
            puts(nodep->emitSimpleOperator());
            puts(" ");
            iterateAndNextNull(nodep->lhsp());
            puts(")");
        } else {
            emitOpName(nodep, nodep->emitC(), nodep->lhsp(), nullptr, nullptr);
        }
    }
    virtual void visit(AstNodeBiop* nodep) override {
        if (nodep->emitCheckMaxWords() && nodep->widthWords() > VL_MULS_MAX_WORDS) {
            nodep->v3warn(
                E_UNSUPPORTED,
                "Unsupported: "
                    << nodep->prettyOperatorName() << " operator of " << nodep->width()
                    << " bits exceeds hardcoded limit VL_MULS_MAX_WORDS in verilatedos.h");
        }
        if (emitSimpleOk(nodep)) {
            putbs("(");
            iterateAndNextNull(nodep->lhsp());
            puts(" ");
            putbs(nodep->emitSimpleOperator());
            puts(" ");
            iterateAndNextNull(nodep->rhsp());
            puts(")");
        } else {
            emitOpName(nodep, nodep->emitC(), nodep->lhsp(), nodep->rhsp(), nullptr);
        }
    }
    virtual void visit(AstNodeTriop* nodep) override {
        UASSERT_OBJ(!emitSimpleOk(nodep), nodep, "Triop cannot be described in a simple way");
        emitOpName(nodep, nodep->emitC(), nodep->lhsp(), nodep->rhsp(), nodep->thsp());
    }
    virtual void visit(AstRedXor* nodep) override {
        if (nodep->lhsp()->isWide()) {
            visit(VN_CAST(nodep, NodeUniop));
        } else {
            putbs("VL_REDXOR_");
            puts(cvtToStr(nodep->lhsp()->dtypep()->widthPow2()));
            puts("(");
            iterateAndNextNull(nodep->lhsp());
            puts(")");
        }
    }
    virtual void visit(AstCCast* nodep) override {
        // Extending a value of the same word width is just a NOP.
        if (nodep->size() <= VL_IDATASIZE) {
            puts("(IData)(");
        } else {
            puts("(QData)(");
        }
        iterateAndNextNull(nodep->lhsp());
        puts(")");
    }
    virtual void visit(AstNodeCond* nodep) override {
        // Widths match up already, so we'll just use C++'s operator w/o any temps.
        if (nodep->expr1p()->isWide()) {
            emitOpName(nodep, nodep->emitC(), nodep->condp(), nodep->expr1p(), nodep->expr2p());
        } else {
            putbs("(");
            iterateAndNextNull(nodep->condp());
            putbs(" ? ");
            iterateAndNextNull(nodep->expr1p());
            putbs(" : ");
            iterateAndNextNull(nodep->expr2p());
            puts(")");
        }
    }
    virtual void visit(AstMemberSel* nodep) override {
        iterateAndNextNull(nodep->fromp());
        putbs("->");
        puts(nodep->varp()->nameProtect());
    }
    virtual void visit(AstNullCheck* nodep) override {
        puts("VL_NULL_CHECK(");
        iterateAndNextNull(nodep->lhsp());
        puts(", ");
        putsQuoted(protect(nodep->fileline()->filename()));
        puts(", ");
        puts(cvtToStr(nodep->fileline()->lineno()));
        puts(")");
    }
    virtual void visit(AstCNew* nodep) override {
        puts("std::make_shared<" + prefixNameProtect(nodep->dtypep()) + ">(");
        puts("vlSymsp");  // TODO make this part of argsp, and eliminate when unnecessary
        if (nodep->argsp()) puts(", ");
        iterateAndNextNull(nodep->argsp());
        puts(")");
    }
    virtual void visit(AstNewCopy* nodep) override {
        puts("std::make_shared<" + prefixNameProtect(nodep->dtypep()) + ">(");
        puts("*");  // i.e. make into a reference
        iterateAndNextNull(nodep->rhsp());
        puts(")");
    }
    virtual void visit(AstSel* nodep) override {
        // Note ASSIGN checks for this on a LHS
        emitOpName(nodep, nodep->emitC(), nodep->fromp(), nodep->lsbp(), nodep->thsp());
    }
    virtual void visit(AstReplicate* nodep) override {
        if (nodep->lhsp()->widthMin() == 1 && !nodep->isWide()) {
            UASSERT_OBJ((static_cast<int>(VN_CAST(nodep->rhsp(), Const)->toUInt())
                         * nodep->lhsp()->widthMin())
                            == nodep->widthMin(),
                        nodep, "Replicate non-constant or width miscomputed");
            puts("VL_REPLICATE_");
            emitIQW(nodep);
            puts("OI(");
            puts(cvtToStr(nodep->widthMin()));
            if (nodep->lhsp()) puts("," + cvtToStr(nodep->lhsp()->widthMin()));
            if (nodep->rhsp()) puts("," + cvtToStr(nodep->rhsp()->widthMin()));
            puts(",");
            iterateAndNextNull(nodep->lhsp());
            puts(", ");
            iterateAndNextNull(nodep->rhsp());
            puts(")");
        } else {
            emitOpName(nodep, nodep->emitC(), nodep->lhsp(), nodep->rhsp(), nullptr);
        }
    }
    virtual void visit(AstStreamL* nodep) override {
        // Attempt to use a "fast" stream function for slice size = power of 2
        if (!nodep->isWide()) {
            uint32_t isPow2 = VN_CAST(nodep->rhsp(), Const)->num().countOnes() == 1;
            uint32_t sliceSize = VN_CAST(nodep->rhsp(), Const)->toUInt();
            if (isPow2 && sliceSize <= (nodep->isQuad() ? sizeof(uint64_t) : sizeof(uint32_t))) {
                puts("VL_STREAML_FAST_");
                emitIQW(nodep);
                emitIQW(nodep->lhsp());
                puts("I(");
                puts(cvtToStr(nodep->widthMin()));
                puts("," + cvtToStr(nodep->lhsp()->widthMin()));
                puts("," + cvtToStr(nodep->rhsp()->widthMin()));
                puts(",");
                iterateAndNextNull(nodep->lhsp());
                puts(", ");
                uint32_t rd_log2 = V3Number::log2b(VN_CAST(nodep->rhsp(), Const)->toUInt());
                puts(cvtToStr(rd_log2) + ")");
                return;
            }
        }
        emitOpName(nodep, "VL_STREAML_%nq%lq%rq(%nw,%lw,%rw, %P, %li, %ri)", nodep->lhsp(),
                   nodep->rhsp(), nullptr);
    }
    virtual void visit(AstCastDynamic* nodep) override {
        putbs("VL_CAST_DYNAMIC(");
        iterateAndNextNull(nodep->lhsp());
        puts(", ");
        iterateAndNextNull(nodep->rhsp());
        puts(")");
    }
    virtual void visit(AstCountBits* nodep) override {
        putbs("VL_COUNTBITS_");
        emitIQW(nodep->lhsp());
        puts("(");
        puts(cvtToStr(nodep->lhsp()->widthMin()));
        puts(", ");
        if (nodep->lhsp()->isWide()) {
            puts(cvtToStr(nodep->lhsp()->widthWords()));  // Note argument width, not node width
                                                          // (which is always 32)
            puts(", ");
        }
        iterateAndNextNull(nodep->lhsp());
        puts(", ");
        iterateAndNextNull(nodep->rhsp());
        puts(", ");
        iterateAndNextNull(nodep->thsp());
        puts(", ");
        iterateAndNextNull(nodep->fhsp());
        puts(")");
    }
    virtual void visit(AstInitItem* nodep) override { iterateChildren(nodep); }
    // Terminals
    virtual void visit(AstVarRef* nodep) override {
        const AstVar* const varp = nodep->varp();
        if (isConstPoolMod(varp->user4p())) {
            // Reference to constant pool variable
            puts(topClassName() + "__ConstPool__");
        } else if (varp->isStatic()) {
            // Access static variable via the containing class
            puts(prefixNameProtect(varp->user4p()) + "::");
        } else if (VN_IS(varp->user4p(), Class) && varp->user4p() != m_modp) {
            // Superclass member reference
            puts(prefixNameProtect(varp->user4p()) + "::");
        } else if (!nodep->selfPointer().empty()) {
            emitDereference(nodep->selfPointerProtect(m_useSelfForThis));
        }
        puts(nodep->varp()->nameProtect());
    }
    virtual void visit(AstAddrOfCFunc* nodep) override {
        // Note: Can be thought to handle more, but this is all that is needed right now
        AstCFunc* const funcp = nodep->funcp();
        UASSERT_OBJ(funcp->isLoose(), nodep, "Cannot take address of non-loose method");
        puts("&");
        puts(funcNameProtect(funcp));
    }
    virtual void visit(AstConst* nodep) override {
        if (nodep->isWide()) {
            UASSERT_OBJ(m_wideTempRefp, nodep, "Wide Constant w/ no temp");
            emitConstant(nodep, m_wideTempRefp, "");
            m_wideTempRefp = nullptr;  // We used it, barf if set it a second time
        } else {
            emitConstant(nodep, nullptr, "");
        }
    }

    //
    virtual void visit(AstMTaskBody* nodep) override {
        VL_RESTORER(m_useSelfForThis);
        m_useSelfForThis = true;
        iterateChildrenConst(nodep);
    }
    virtual void visit(AstConsAssoc* nodep) override {
        putbs(nodep->dtypep()->cType("", false, false));
        puts("()");
        if (nodep->defaultp()) {
            putbs(".setDefault(");
            iterateAndNextNull(nodep->defaultp());
            puts(")");
        }
    }
    virtual void visit(AstSetAssoc* nodep) override {
        iterateAndNextNull(nodep->lhsp());
        putbs(".set(");
        iterateAndNextNull(nodep->keyp());
        puts(", ");
        putbs("");
        iterateAndNextNull(nodep->valuep());
        puts(")");
    }
    virtual void visit(AstConsDynArray* nodep) override {
        putbs(nodep->dtypep()->cType("", false, false));
        if (!nodep->lhsp()) {
            puts("()");
        } else {
            puts("::cons(");
            iterateAndNextNull(nodep->lhsp());
            if (nodep->rhsp()) {
                puts(", ");
                putbs("");
            }
            iterateAndNextNull(nodep->rhsp());
            puts(")");
        }
    }
    virtual void visit(AstConsQueue* nodep) override {
        putbs(nodep->dtypep()->cType("", false, false));
        if (!nodep->lhsp()) {
            puts("()");
        } else {
            puts("::cons(");
            iterateAndNextNull(nodep->lhsp());
            if (nodep->rhsp()) {
                puts(", ");
                putbs("");
            }
            iterateAndNextNull(nodep->rhsp());
            puts(")");
        }
    }
    virtual void visit(AstCReset* nodep) override {
        AstVar* varp = nodep->varrefp()->varp();
        emitVarReset(varp);
    }
    virtual void visit(AstExecGraph* nodep) override {
        UASSERT_OBJ(nodep == v3Global.rootp()->execGraphp(), nodep,
                    "ExecGraph should be a singleton!");
        // The location of the AstExecGraph within the containing _eval()
        // function is where we want to invoke the graph and wait for it to
        // complete. Emitting the children does just that.
        iterateChildrenConst(nodep);
    }
    virtual void visit(AstChangeDet* nodep) override {  //
        m_blkChangeDetVec.push_back(nodep);
    }

    // Just iterate
    virtual void visit(AstNetlist* nodep) override { iterateChildren(nodep); }
    virtual void visit(AstTopScope* nodep) override { iterateChildren(nodep); }
    virtual void visit(AstScope* nodep) override { iterateChildren(nodep); }
    // NOPs
    virtual void visit(AstTypedef*) override {}
    virtual void visit(AstPragma*) override {}
    virtual void visit(AstCell*) override {}  // Handled outside the Visit class
    virtual void visit(AstVar*) override {}  // Handled outside the Visit class
    virtual void visit(AstNodeText*) override {}  // Handled outside the Visit class
    virtual void visit(AstTraceDecl*) override {}  // Handled outside the Visit class
    virtual void visit(AstTraceInc*) override {}  // Handled outside the Visit class
    virtual void visit(AstCFile*) override {}  // Handled outside the Visit class
    virtual void visit(AstCellInline*) override {}  // Handled outside visit (in EmitCSyms)
    virtual void visit(AstCUse*) override {}  // Handled outside the Visit class
    // Default
    virtual void visit(AstNode* nodep) override {
        puts(string("\n???? // ") + nodep->prettyTypeName() + "\n");
        iterateChildren(nodep);
        if (!v3Global.opt.lintOnly()) {  // An internal problem, so suppress
            nodep->v3fatalSrc("Unknown node type reached emitter: " << nodep->prettyTypeName());
        }
    }

    EmitCFunc()
        : m_lazyDecls(*this) {
        m_suppressSemi = false;
        m_wideTempRefp = nullptr;
        m_labelNum = 0;
        m_splitSize = 0;
        m_splitFilenum = 0;
    }
    EmitCFunc(AstNode* nodep, V3OutCFile* ofp, bool trackText = false)
        : EmitCFunc{} {
        m_ofp = ofp;
        m_trackText = trackText;
        iterate(nodep);
    }
    virtual ~EmitCFunc() override = default;
};

#endif  // guard
