/************************************************************************
 ************************************************************************
    FAUST compiler
	Copyright (C) 2003-2004 GRAME, Centre National de Creation Musicale
    ---------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ************************************************************************
 ************************************************************************/

#include <string>
#include <fstream>

#include "code_container.hh"
#include "recursivness.hh"
#include "floats.hh"
#include "global.hh"
#include "type_manager.hh"
#include "fir_to_fir.hh"

using namespace std;

string makeDrawPath();

void CodeContainer::initializeCodeContainer(int numInputs, int numOutputs)
{
    fNumInputs = numInputs;
    fNumOutputs = numOutputs;
    fInputRates.resize(numInputs, 0);
    fOutputRates.resize(numOutputs, 0);
    fJSON.setInputs(numInputs);
    fJSON.setOutputs(numOutputs);
}

CodeContainer::CodeContainer()
    :fParentContainer(0), fNumInputs(-1), fNumOutputs(-1),
    fExtGlobalDeclarationInstructions(InstBuilder::genBlockInst()),
    fGlobalDeclarationInstructions(InstBuilder::genBlockInst()),
    fDeclarationInstructions(InstBuilder::genBlockInst()),
    fInitInstructions(InstBuilder::genBlockInst()),
    fPostInitInstructions(InstBuilder::genBlockInst()),
    fAllocateInstructions(InstBuilder::genBlockInst()),
    fDestroyInstructions(InstBuilder::genBlockInst()),
    fStaticInitInstructions(InstBuilder::genBlockInst()),
    fPostStaticInitInstructions(InstBuilder::genBlockInst()),
    fComputeBlockInstructions(InstBuilder::genBlockInst()),
    fComputeFunctions(InstBuilder::genBlockInst()),
    fUserInterfaceInstructions(InstBuilder::genBlockInst()),
    fSubContainerType(kInt), fFullCount("count"),
    fGeneratedSR(false),
    fJSON(-1, -1)
{
    fCurLoop = new CodeLoop(0, "i");
}

CodeContainer::~CodeContainer()
{}

void CodeContainer::transformDAG(DispatchVisitor* visitor)
{
    lclgraph G;
    CodeLoop::sortGraph(fCurLoop, G);
    for (int l = G.size() - 1; l >= 0; l--) {
        for (lclset::const_iterator p = G[l].begin(); p != G[l].end(); p++) {
            (*p)->transform(visitor);
        }
    }
}

/**
 * Store the loop used to compute a signal
 */
void CodeContainer::setLoopProperty(Tree sig, CodeLoop* l)
{
    fLoopProperty.set(sig, l);
}

/**
 * Returns the loop used to compute a signal
 */
bool CodeContainer::getLoopProperty(Tree sig, CodeLoop*& l)
{
    assert(sig);
    return fLoopProperty.get(sig, l);
}

/**
 * Open a non-recursive loop on top of the stack of open loops.
 * @param size the number of iterations of the loop
 */
void CodeContainer::openLoop(string index_name, int size)
{
    fCurLoop = new CodeLoop(fCurLoop, index_name, size);
}

/**
 * Open a recursive loop on top of the stack of open loops.
 * @param recsymbol the recursive symbol defined in this loop
 * @param size the number of iterations of the loop
 */
void CodeContainer::openLoop(Tree recsymbol, string index_name, int size)
{
     fCurLoop = new CodeLoop(recsymbol, fCurLoop, index_name, size);
}

/**
 * Close the top loop and either keep it
 * or absorb it within its enclosing loop.
 */
void CodeContainer::closeLoop(Tree sig)
{
    assert(fCurLoop);
    CodeLoop* l = fCurLoop;
    fCurLoop = l->fEnclosingLoop;
    assert(fCurLoop);

    Tree S = symlist(sig);
    if (l->isEmpty() || fCurLoop->hasRecDependencyIn(S)) {
        fCurLoop->absorb(l);
    } else {
        // cout << " will NOT absorb" << endl;
        // we have an independent loop
        setLoopProperty(sig, l);     // associate the signal
        fCurLoop->fBackwardLoopDependencies.insert(l);
        // we need to indicate that all recursive symbols defined
        // in this loop are defined in this loop
        for (Tree lsym = l->fRecSymbolSet; !isNil(lsym); lsym = tl(lsym)) {
            this->setLoopProperty(hd(lsym), l);
            //cerr << "loop " << l << " defines " << *hd(lsym) << endl;
        }
    }
}

/**
 * Print the required C++ libraries as comments in source code
 */
void CodeContainer::printLibrary(ostream& fout)
{
	set<string> S;
	set<string>::iterator f;

	string sep;
	collectLibrary(S);
    if (S.size() > 0) {
        fout << "/* link with ";
        for (f = S.begin(), sep =": "; f != S.end(); f++, sep = ", ") {
            fout << sep << *f;
        }
        fout << " */\n";
    }
}

/**
 * Print the required include files
 */
void CodeContainer::printIncludeFile(ostream& fout)
{
    set<string> S;
    set<string>::iterator f;

    collectIncludeFile(S);
    for (f = S.begin(); f != S.end(); f++) {
        string inc = *f;
        // Only print non-empty include (inc is actually quoted)
        if (inc.size() > 2) { 
            fout << "#include " << *f << "\n";
        }
    }
}

/**
 * Print the loop graph in dot format
 */
void CodeContainer::printGraphDotFormat(ostream& fout)
{
    lclgraph G;
    CodeLoop::sortGraph(fCurLoop, G);

    fout << "strict digraph loopgraph {" << endl;
    fout << '\t' << "rankdir=LR;" << endl;
    fout << '\t' << "node[color=blue, fillcolor=lightblue, style=filled, fontsize=9];" << endl;

    int lnum = 0;       // used for loop numbers
    // for each level of the graph
    for (int l = G.size() - 1; l >= 0; l--) {
        // for each task in the level
        for (lclset::const_iterator t = G[l].begin(); t!=G[l].end(); t++) {
            // print task label "Lxxx : 0xffffff"
            fout << '\t' << 'L'<<(*t)<<"[label=<<font face=\"verdana,bold\">L"<<lnum++<<"</font> : "<<(*t)<<">];"<<endl;
            // for each source of the task
            for (lclset::const_iterator src = (*t)->fBackwardLoopDependencies.begin(); src!=(*t)->fBackwardLoopDependencies.end(); src++) {
                // print the connection Lxxx -> Lyyy;
                fout << '\t' << 'L'<<(*src)<<"->"<<'L'<<(*t)<<';'<<endl;
            }
        }
    }
    fout << "}" << endl;
}

/**
 *  Adds forward dependencies in the DAG and returns loop count
 */
void CodeContainer::computeForwardDAG(lclgraph dag, int& loop_count, vector<int>& ready_loop)
{
    #define START_TASK_MAX 2

    int loop_index = START_TASK_MAX; // First index to be used for remaining tasks

    for (int l = dag.size() - 1; l >= 0; l--) {
        for (lclset::const_iterator p = dag[l].begin(); p != dag[l].end(); p++) {
            
            // Setup forward dependancy
            for (lclset::const_iterator p1 = (*p)->fBackwardLoopDependencies.begin(); p1!=(*p)->fBackwardLoopDependencies.end(); p1++) {
                (*p1)->fForwardLoopDependencies.insert((*p));
            }
            
            // Setup loop index
            (*p)->fIndex = loop_index;
            loop_index++;
            
            // Keep ready loops
            if ((*p)->getBackwardLoopDependencies().size() == 0) {
                ready_loop.push_back((*p)->getIndex());
            }
        }
    }
    
    loop_count = loop_index;
}

static inline BasicTyped* getTypeASM(Typed::VarType result)
{
    if (result == Typed::kInt) {
        return InstBuilder::genBasicTyped(Typed::kIntish);
    } else if ((result == Typed::kFloatMacro || result == Typed::kFloat)) {
        return InstBuilder::genBasicTyped(Typed::kFloatish);
    } else if ((result == Typed::kDouble)) {
        return InstBuilder::genBasicTyped(Typed::kDoublish);
    } else {
        return InstBuilder::genBasicTyped(result);
    }
}

ValueInst* CodeContainer::pushFunction(const string& name, Typed::VarType result, vector<Typed::VarType>& types, const list<ValueInst*>& args)
{
    BasicTyped* result_type = InstBuilder::genBasicTyped(result);

    // Special case for "faustpower", generates sequence of multiplication
    if (name == "faustpower") {
        
        list<ValueInst*>::const_iterator it = args.begin(); it++;
        IntNumInst* arg1 = dynamic_cast<IntNumInst*>(*it);
        
        stringstream num; num << arg1->fNum;
        string faust_power_name = name + num.str() + ((result == Typed::kInt) ? "_i" : "_f");
 
        list<NamedTyped*> named_args;
        named_args.push_back(InstBuilder::genNamedTyped("value", InstBuilder::genBasicTyped(types[0])));

        // Expand the pow depending of the exposant argument
        BlockInst* block = InstBuilder::genBlockInst();

        if (arg1->fNum == 0) {
             block->pushBackInst(InstBuilder::genRetInst(InstBuilder::genIntNumInst(1)));
        } else {
            ValueInst* res = InstBuilder::genLoadFunArgsVar("value");
            for (int i= 0; i < arg1->fNum - 1; i++) {
                res = InstBuilder::genMul(res, InstBuilder::genLoadFunArgsVar("value"));
            }
            // Use cast to "keep" result type
            if (gGlobal->gOutputLang == "ajs") {
                block->pushBackInst(InstBuilder::genRetInst(InstBuilder::genCastNumInst(res, getTypeASM(result))));
            } else {
                block->pushBackInst(InstBuilder::genRetInst(res));
            }
        }
        
        pushGlobalDeclare(InstBuilder::genDeclareFunInst(faust_power_name, InstBuilder::genFunTyped(named_args, result_type), block));

        list<ValueInst*> truncated_args;
        truncated_args.push_back((*args.begin()));
        if (gGlobal->gOutputLang == "ajs") {
            // Use cast to "keep" result type
            return InstBuilder::genCastNumInst(InstBuilder::genFunCallInst(faust_power_name, truncated_args), getTypeASM(result));
        } else {
            return InstBuilder::genFunCallInst(faust_power_name, truncated_args);
        }
    } else {
      
        list<NamedTyped*> named_args;
        for (size_t i = 0; i < types.size(); i++) {
            stringstream num; num << i;
            named_args.push_back(InstBuilder::genNamedTyped("dummy" + num.str(), InstBuilder::genBasicTyped(types[i])));
        }

        pushGlobalDeclare(InstBuilder::genDeclareFunInst(name, InstBuilder::genFunTyped(named_args, result_type)));
        if (gGlobal->gOutputLang == "ajs") {
            // Use cast to "keep" result type
            return InstBuilder::genCastNumInst(InstBuilder::genFunCallInst(name, args), getTypeASM(result));
        } else {
            return InstBuilder::genFunCallInst(name, args);
        }
    }
}

void CodeContainer::sortDeepFirstDAG(CodeLoop* l, set<CodeLoop*>& visited, list<CodeLoop*>& result)
{
	// Avoid printing already printed loops
	if (isElement(visited, l)) return;

	// Remember we have printed this loop
	visited.insert(l);

	// Compute the dependencies loops (that need to be computed before this one)
	for (lclset::const_iterator p = l->fBackwardLoopDependencies.begin(); p != l->fBackwardLoopDependencies.end(); p++) {
        sortDeepFirstDAG(*p, visited, result);
    }

    // Keep the non-empty loops in result
    if (!l->isEmpty()) {
        result.push_back(l);
    }
}

void CodeContainer::generateLocalInputs(BlockInst* loop_code, const string& index)
{
    // Generates line like: FAUSTFLOAT* input0 = &input0_ptr[index];
    for (int i = 0; i < inputs(); i++) {
        string name1 = subst("fInput$0", T(i));
        string name2 = subst("fInput$0_ptr", T(i));
        loop_code->pushBackInst(InstBuilder::genStoreStackVar(name1, InstBuilder::genLoadArrayStructVarAddress(name2, InstBuilder::genLoadLoopVar(index))));
    }
}

void CodeContainer::generateLocalOutputs(BlockInst* loop_code, const string& index)
{
    // Generates line like: FAUSTFLOAT* ouput0 = &ouput0_ptr[index];
    for (int i = 0; i < outputs(); i++) {
        string name1 = subst("fOutput$0", T(i));
        string name2 = subst("fOutput$0_ptr", T(i));
        loop_code->pushBackInst(InstBuilder::genStoreStackVar(name1, InstBuilder::genLoadArrayStructVarAddress(name2, InstBuilder::genLoadLoopVar(index))));
    }
}

DeclareFunInst* CodeContainer::generateGetIO(const string& name, int io, bool ismethod, bool isvirtual)
{
    list<NamedTyped*> args;
    if (!ismethod) {
        args.push_back(InstBuilder::genNamedTyped("dsp", Typed::kObj_ptr));
    }
    BlockInst* block = InstBuilder::genBlockInst();
    block->pushBackInst(InstBuilder::genRetInst(InstBuilder::genIntNumInst(io)));

    // Creates function
    FunTyped* fun_type = InstBuilder::genFunTyped(args, InstBuilder::genBasicTyped(Typed::kInt), (isvirtual) ? FunTyped::kVirtual : FunTyped::kDefault);
    return InstBuilder::genDeclareFunInst(name, fun_type, block);
}

DeclareFunInst* CodeContainer::generateGetInputs(const string& name, bool ismethod, bool isvirtual)
{
    return generateGetIO(name, fNumInputs, ismethod, isvirtual);
}

DeclareFunInst* CodeContainer::generateGetOutputs(const string& name, bool ismethod, bool isvirtual)
{
    return generateGetIO(name, fNumOutputs, ismethod, isvirtual);
}

DeclareFunInst* CodeContainer::generateGetIORate(const string& name, vector<int>& io, bool ismethod, bool isvirtual)
{
    list<NamedTyped*> args;
    if (!ismethod) {
        args.push_back(InstBuilder::genNamedTyped("dsp", Typed::kObj_ptr));
    }
    args.push_back(InstBuilder::genNamedTyped("channel", Typed::kInt));

    BlockInst* code = InstBuilder::genBlockInst();
    ValueInst* switch_cond = InstBuilder::genLoadFunArgsVar("channel");
    ::SwitchInst* switch_block = InstBuilder::genSwitchInst(switch_cond);
    code->pushBackInst(InstBuilder::genDecStackVar("rate", InstBuilder::genBasicTyped(Typed::kInt)));
    code->pushBackInst(switch_block);

    int i = 0;
    for (vector<int>::const_iterator it = io.begin(); it != io.end(); it++, i++) {
        // Creates "case" block
        BlockInst* case_block = InstBuilder::genBlockInst();
        // Compiles "case" block
        case_block->pushBackInst(InstBuilder::genStoreStackVar("rate", InstBuilder::genIntNumInst(*it)));
        // Add it into the switch
        switch_block->addCase(i, case_block);
    }

    // Default case
    BlockInst* default_case_block = InstBuilder::genBlockInst();
    default_case_block->pushBackInst(InstBuilder::genStoreStackVar("rate", InstBuilder::genIntNumInst(-1)));
    switch_block->addCase(-1, default_case_block);
    
    // Return "rate" result
    code->pushBackInst(InstBuilder::genRetInst(InstBuilder::genLoadStackVar("rate")));

    // Creates function
    FunTyped* fun_type = InstBuilder::genFunTyped(args, InstBuilder::genBasicTyped(Typed::kInt), (isvirtual) ? FunTyped::kVirtual : FunTyped::kDefault);
    return InstBuilder::genDeclareFunInst(name, fun_type, code);
}

DeclareFunInst* CodeContainer::generateGetInputRate(const string& name, bool ismethod, bool isvirtual)
{
    return generateGetIORate(name, fInputRates, ismethod, isvirtual);
}

DeclareFunInst* CodeContainer::generateGetOutputRate(const string& name, bool ismethod, bool isvirtual)
{
    return generateGetIORate(name, fOutputRates, ismethod, isvirtual);
}

void CodeContainer::generateDAGLoopInternal(CodeLoop* loop, BlockInst* block, DeclareVarInst* count, bool omp)
{
    if (gGlobal->gVecLoopSize > 0 && !loop->fIsRecursive) {
        loop->generateDAGVectorLoop(block, count, omp, gGlobal->gVecLoopSize);
    } else {
        loop->generateDAGScalarLoop(block, count, omp);
    }
}

void CodeContainer::generateDAGLoopAux(CodeLoop* loop, BlockInst* loop_code, DeclareVarInst* count, int loop_num, bool omp)
{
    if (gGlobal->gFunTaskSwitch) {
        BlockInst* block = InstBuilder::genBlockInst();
        // Generates scalar or vectorized loop
        generateDAGLoopInternal(loop, block, count, omp);
        Loop2FunctionBuider builder(subst("fun$0" + getClassName(), T(loop_num)), block, gGlobal->gDSPStruct);
        pushOtherComputeMethod(builder.fFunctionDef);
        loop_code->pushBackInst(InstBuilder::genLabelInst((loop->fIsRecursive) ? subst("/* Recursive function $0 */", T(loop_num)) : subst("/* Vectorizable function $0 */", T(loop_num))));
        loop_code->pushBackInst(builder.fFunctionCall);
    } else {
        loop_code->pushBackInst(InstBuilder::genLabelInst((loop->fIsRecursive) ? subst("/* Recursive loop $0 */", T(loop_num)) : subst("/* Vectorizable loop $0 */", T(loop_num))));
        // Generates scalar or vectorized loop
        generateDAGLoopInternal(loop, loop_code, count, omp);
    }
}

void CodeContainer::generateDAGLoop(BlockInst* block, DeclareVarInst* count)
{
    int loop_num = 0;

    if (gGlobal->gDeepFirstSwitch) {
        set<CodeLoop*> visited;
        list<CodeLoop*> result;
        sortDeepFirstDAG(fCurLoop, visited, result);
        for (list<CodeLoop*>::const_iterator p = result.begin(); p != result.end(); p++) {
            generateDAGLoopAux(*p, block, count, loop_num++);
        }
    } else {
        lclgraph G;
        CodeLoop::sortGraph(fCurLoop, G);
        for (int l = G.size() - 1; l >= 0; l--) {
            for (lclset::const_iterator p = G[l].begin(); p != G[l].end(); p++) {
                generateDAGLoopAux(*p, block, count, loop_num++);
            }
        }
    }
}

void CodeContainer::processFIR(void)
{
    if (gGlobal->gGroupTaskSwitch) {
        CodeLoop::computeUseCount(fCurLoop);
        CodeLoop::groupSeqLoops(fCurLoop);
    }
}

BlockInst* CodeContainer::flattenFIR(void)
{
    BlockInst* global_block = InstBuilder::genBlockInst();
    
    // Declaration part
    global_block->merge(fExtGlobalDeclarationInstructions);
    global_block->merge(fGlobalDeclarationInstructions);
    global_block->merge(fDeclarationInstructions);
    
    // Init method
    global_block->merge(fInitInstructions);
    global_block->merge(fPostInitInstructions);
    global_block->merge(fStaticInitInstructions);
    global_block->merge(fPostStaticInitInstructions);
    
    // Subcontainers
    list<CodeContainer*>::const_iterator it;
    for (it = fSubContainers.begin(); it != fSubContainers.end(); it++) {
        global_block->merge((*it)->flattenFIR());
    }
   
    // Compute method
    global_block->merge(fComputeBlockInstructions);
    return global_block;
}

void CodeContainer::generateDeclarations(InstVisitor* visitor)
{
#ifndef _WIN32
    fDeclarationInstructions->fCode.sort(sortArrayDeclarations);
#endif
    handleDeclarations(visitor);
}

void CodeContainer::generateMetaData(JSONUI* json)
{
    // Add global metadata
    for (map<Tree, set<Tree> >::iterator i = gGlobal->gMetaDataSet.begin(); i != gGlobal->gMetaDataSet.end(); i++) {
        if (i->first != tree("author")) {
            stringstream str1, str2;
            str1 << *(i->first);
            str2 << **(i->second.begin());
            json->declare(str1.str().c_str(), unquote(str2.str()).c_str());
        } else {
            for (set<Tree>::iterator j = i->second.begin(); j != i->second.end(); j++) {
                if (j == i->second.begin()) {
                    stringstream str1, str2;
                    str1 << *(i->first);
                    str2 << **j;
                    json->declare(str1.str().c_str(), unquote(str2.str()).c_str());
                } else {
                    stringstream str2;
                    str2 << **j;
                    json->declare("contributor", unquote(str2.str()).c_str());
                }
            }
        }
    }
}

void CodeContainer::generateJSON()
{
    if (gGlobal->gPrintJSONSwitch) {
    
        // Add global metadata
        generateMetaData(&fJSON);
      
        // Generate JSON
        generateUserInterface(&fJSON);
        ofstream xout(subst("$0.json", makeDrawPath()).c_str());
        xout << fJSON.JSON();
    } 
}

