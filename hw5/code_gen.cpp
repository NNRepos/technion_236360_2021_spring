#include "code_gen.h"

CodeGen::CodeGen(SemanticChecks &semantic_ref) : register_count(0), code_buffer(), semantic_ref(semantic_ref) {
    EmitGlobalFunctions();
}

register_name CodeGen::GenRegister() {
    register_name ret;
    return (ret + "%r" + to_string(register_count++));
}

register_name CodeGen::GenGlobalRegister() {
    register_name ret;
    return (ret + "@r" + to_string(register_count++));
}

void CodeGen::EmitGlobalFunctions() {
    // printf
    code_buffer.emitGlobal("declare i32 @printf(i8*, ...)");

    // exit
    code_buffer.emitGlobal("declare void @exit(i32)");

    // print
    code_buffer.emitGlobal(R"(@.str_specifier = constant [4 x i8] c"%s\0A\00")");
    code_buffer.emitGlobal("define void @print(i8*) {");
    code_buffer.emitGlobal("    %spec_ptr = getelementptr [4 x i8], [4 x i8]* @.str_specifier, i32 0, i32 0");
    code_buffer.emitGlobal("    call i32 (i8*, ...) @printf(i8* %spec_ptr, i8* %0)");
    code_buffer.emitGlobal("    ret void");
    code_buffer.emitGlobal("}");

    // printi
    code_buffer.emitGlobal(R"(@.int_specifier = constant [4 x i8] c"%d\0A\00")");
    code_buffer.emitGlobal("define void @printi(i32) {");
    code_buffer.emitGlobal("    %spec_ptr = getelementptr [4 x i8], [4 x i8]* @.int_specifier, i32 0, i32 0");
    code_buffer.emitGlobal("    call i32 (i8*, ...) @printf(i8* %spec_ptr, i32 %0)");
    code_buffer.emitGlobal("    ret void");
    code_buffer.emitGlobal("}");

    // zero
    code_buffer.emitGlobal(R"(@err_zero = constant [23 x i8] c"Error division by zero\00")");

}

STypeRegisterPtr CodeGen::EmitBinop(const STypePtr &exp1, string binop, const STypePtr &exp2) {
    auto dynamic_cast_exp1 = GetNonBoolExpString(exp1);
    auto dynamic_cast_exp2 = GetNonBoolExpString(exp2);

    auto reg_result = make_shared<STypeRegister>(GenRegister(), INT_TYPE);

    if (binop == "/") {
        EmitCheckDivZero(exp2);
    }

    string binop_str = reg_result->reg_name + " = ";
    switch (binop[0]) {
        case '+':
            binop_str += "add ";
            break;
        case '-':
            binop_str += "sub ";
            break;
        case '*':
            binop_str += "mul ";
            break;
        case '/':
            binop_str += "sdiv ";
            break;
    }

    // all types are i32
    binop_str += "i32 ";

    binop_str += dynamic_cast_exp1;

    binop_str += ", ";

    binop_str += dynamic_cast_exp2;

    code_buffer.emit(binop_str);

    if (semantic_ref.CheckAndGetBinOpType(exp1->general_type, exp2->general_type) == BYTE_TYPE) {
        // byte < 256
        auto reg_old_result = reg_result->reg_name;
        reg_result->reg_name = GenRegister();
        reg_result->general_type = BYTE_TYPE;
        code_buffer.emit(reg_result->reg_name + " = and i32 " + reg_old_result + ", 255");
    }

    return reg_result;
}

void CodeGen::EmitCheckDivZero(const STypePtr &exp) {
    auto dynamic_cast_reg_exp = dynamic_pointer_cast<STypeRegister>(exp);
    auto dynamic_cast_num_exp = dynamic_pointer_cast<STypeNumber>(exp);

    auto is_zero = GenRegister();
    auto err_str = GenRegister();

    if (dynamic_cast_reg_exp) {
        code_buffer.emit(is_zero + " = icmp eq i32 " + dynamic_cast_reg_exp->reg_name + ", 0");
    } else {
        code_buffer.emit(is_zero + " = icmp eq i32 " + to_string(dynamic_cast_num_exp->token) + ", 0");
    }

    auto bp_zero_and_non = code_buffer.emit("br i1 " + is_zero + ", label @, label @");
    auto label_err_zero = code_buffer.genLabel("_err_zero");
    vector<pair<int, BranchLabelIndex>> bp_zero_vec;
    bp_zero_vec.emplace_back(bp_zero_and_non, FIRST);
    code_buffer.bpatch(bp_zero_vec, label_err_zero);

    code_buffer.emit(err_str + " = getelementptr [23 x i8], [23 x i8]* @err_zero, i32 0, i32 0");
    code_buffer.emit("call void (i8*) @print(i8* " + err_str + ")");
    code_buffer.emit("call void (i32) @exit(i32 0)");
    code_buffer.emit("unreachable");  // this prevents no branch before label error

    auto label_non_zero = code_buffer.genLabel("_non_zero");
    vector<pair<int, BranchLabelIndex>> bp_non_vec;
    bp_non_vec.emplace_back(bp_zero_and_non, SECOND);
    code_buffer.bpatch(bp_non_vec, label_non_zero);
}

STypeRegisterPtr CodeGen::EmitCall(const STypeFunctionSymbolPtr &func, const STypeExpListPtr &exp_list) {
    auto reg_result = make_shared<STypeRegister>(GenRegister(), func->ret_type);
    string out_str;

    if (func->ret_type != VOID_TYPE) {
        out_str += reg_result->reg_name + " = ";
    }

    out_str += "call ";
    string llvm_type = GetLLVMType(func->ret_type);
    out_str += llvm_type + " @" + func->name + "(";

    for (size_t i = 0; i < exp_list->exp_list.size(); ++i) {
        if (i > 0) {
            out_str += ", ";
        }
        out_str += GetLLVMType(exp_list->exp_list[i]->general_type) + " ";

        {
            out_str += GetNonBoolExpString(exp_list->exp_list[i]);
        }
    }
    out_str += ")";

    code_buffer.emit(out_str);
    return reg_result;
}

STypeRegisterPtr CodeGen::EmitCall(const STypeFunctionSymbolPtr &func) {
    auto reg_result = make_shared<STypeRegister>(GenRegister(), func->ret_type);
    string out_str;

    if (func->ret_type != VOID_TYPE) {
        out_str += reg_result->reg_name + " = ";
    }

    out_str += "call ";
    string llvm_type = GetLLVMType(func->ret_type);

    out_str += llvm_type + " @" + func->name + "()";

    code_buffer.emit(out_str);
    return reg_result;
}

string CodeGen::GetLLVMType(const Type &type) {
    string llvm_type;
    switch (type) {
        case VOID_TYPE:
            llvm_type = "void";
            break;
        case STRING_TYPE:
            llvm_type = "i8*";
            break;
        default:
            llvm_type = "i32";
            break;
    }
    return llvm_type;
}

string CodeGen::GetNonBoolExpString(const STypePtr &exp) {
    // supports bools that are in register representation
    auto dynamic_cast_num_exp = dynamic_pointer_cast<STypeNumber>(exp);
    auto dynamic_cast_reg_exp = dynamic_pointer_cast<STypeRegister>(exp);
    auto dynamic_cast_str_exp = dynamic_pointer_cast<STypeString>(exp);
    auto dynamic_cast_sym_exp = dynamic_pointer_cast<STypeSymbol>(exp);


    if (dynamic_cast_num_exp) {
        return to_string(dynamic_cast_num_exp->token);
    } else if (dynamic_cast_reg_exp) {
        return dynamic_cast_reg_exp->reg_name;
    } else if (dynamic_cast_sym_exp) {
        return EmitLoadRegister(dynamic_cast_sym_exp->offset, dynamic_cast_sym_exp->general_type)->reg_name;
    } else {
        assert(false);
    }
}

void CodeGen::EmitProgram() {
    if (PRINT_DEBUG) {
        cout << "code starts here:\n";
    }
    code_buffer.printGlobalBuffer();
    code_buffer.printCodeBuffer();
}

STypeBoolExpPtr CodeGen::EmitRelOp(const STypePtr &exp1, STypePtr &relop, const STypePtr &exp2) {
    auto exp1_value = GetNonBoolExpString(exp1);
    auto exp2_value = GetNonBoolExpString(exp2);

    auto reg_icmp = GenRegister();
    string icmp_string(reg_icmp + " = icmp ");

    auto dynamic_cast_relop = dynamic_pointer_cast<STypeString>(relop);
    auto relop_str = dynamic_cast_relop->token;

    if (relop_str == "<") {
        icmp_string += "slt";
    } else if (relop_str == ">") {
        icmp_string += "sgt";
    } else if (relop_str == "<=") {
        icmp_string += "sle";
    } else if (relop_str == ">=") {
        icmp_string += "sge";
    } else if (relop_str == "==") {
        icmp_string += "eq";
    } else if (relop_str == "!=") {
        icmp_string += "ne";
    } else {
        assert(false);
    }

    icmp_string += " i32 " + exp1_value + ", " + exp2_value;
    code_buffer.emit(icmp_string);
    auto branch_addr = code_buffer.emit("br i1 " + reg_icmp + ", label @, label @");

    auto true_list = CodeBuffer::makelist({branch_addr, FIRST});
    auto false_list = CodeBuffer::makelist({branch_addr, SECOND});

    return make_shared<STypeBoolExp>(true_list, false_list);
}

STypeStatementPtr CodeGen::EmitStatementType(string id) {
    auto symbol = semantic_ref.table_ref.GetDefinedSymbol(id);
    auto statement = make_shared<STypeStatement>(branch_list());

    // create default value and store in stack
    // r = 0+0 (including bool)
    auto reg_default_value = GenRegister();
    code_buffer.emit(reg_default_value + " = add i32 0, 0");

    EmitStoreRegister(symbol->offset, reg_default_value);

    return statement;
}

STypeStatementPtr CodeGen::EmitStatementAssign(string id, const STypePtr &exp) {
    auto symbol = semantic_ref.table_ref.GetDefinedSymbol(id);
    auto statement = make_shared<STypeStatement>(branch_list());
    auto reg_result = GenRegister();

    if (exp->general_type == BOOL_TYPE) {
        EmitBoolExpToRegister(exp, reg_result);

    } else {
        auto exp_str = GetNonBoolExpString(exp);
        code_buffer.emit(reg_result + " = add i32 " + exp_str + ", 0");
    }

    // store and return
    EmitStoreRegister(symbol->offset, reg_result);

    return statement;
}

void CodeGen::EmitBoolExpToRegister(const STypePtr &exp, const register_name &reg_result) {
    auto dynamic_cast_bool_exp = dynamic_pointer_cast<STypeBoolExp>(exp);

    // create phi junction
    auto true_label = code_buffer.genLabel("_convert_true");
    auto bp_true = code_buffer.emit("br label @");
    auto false_label = code_buffer.genLabel("_convert_false");
    auto bp_false = code_buffer.emit("br label @");
    auto convert_label = code_buffer.genLabel("_convert_final");

    auto convert_true_list = CodeBuffer::makelist(branch_pair(bp_true, FIRST));
    auto convert_false_list = CodeBuffer::makelist(branch_pair(bp_false, FIRST));

    code_buffer.bpatch(dynamic_cast_bool_exp->true_list, true_label);
    code_buffer.bpatch(dynamic_cast_bool_exp->false_list, false_label);
    code_buffer.bpatch(convert_true_list, convert_label);
    code_buffer.bpatch(convert_false_list, convert_label);

    code_buffer.emit(reg_result + " = phi i32 [1, %" + true_label + "], [0, %" + false_label + "]");
}

void CodeGen::EmitFuncHead(const STypeFunctionSymbolPtr &symbol) {
    // emit definition
    string emit_string("define ");

    if (symbol->ret_type == VOID_TYPE) {
        emit_string += "void @";
    } else {
        emit_string += "i32 @";
    }

    emit_string += symbol->name;
    emit_string += "(";

    for (size_t i = 0; i < symbol->parameters.size(); ++i) {
        if (i > 0) {
            emit_string += ", ";
        }
        emit_string += "i32";
    }

    emit_string += ") {";

    code_buffer.emit(emit_string);

    // allocate stack
    stack_register = GenRegister();
    code_buffer.emit(stack_register + " = alloca i32, i32 " + to_string(STACK_SIZE));
}

void CodeGen::EmitStoreRegister(int offset, const register_name &reg_to_store) {
    assert(!stack_register.empty());
    auto reg_pointer_to_stack = GenRegister();

    if (offset >= 0) {
        // get address on stack
        code_buffer.emit(reg_pointer_to_stack + " = getelementptr i32, i32* " + stack_register +
                         ", i32 " + to_string(offset));

        // store in stack
        code_buffer.emit("store i32 " + reg_to_store + ", i32* " + reg_pointer_to_stack);
    }
}

STypeRegisterPtr CodeGen::EmitLoadRegister(int offset, Type type) {
    assert(!stack_register.empty());

    auto reg_result = GenRegister();

    if (offset >= 0) {
        // non-negative offset - calculate real address and load
        auto reg_stack_offset = GenRegister();

        code_buffer.emit(reg_stack_offset + " = getelementptr i32, i32* " +
                         stack_register + ", i32 " + to_string(offset));
        code_buffer.emit(reg_result + " = load i32, i32* " + reg_stack_offset);

    } else {
        // negative offset - get func register and assign
        auto reg_argument = "%" + to_string(-offset - 1);
        code_buffer.emit(reg_result + " = add i32 0, " + reg_argument);
    }

    return make_shared<STypeRegister>(reg_result, type);

}

STypePtr CodeGen::RegisterToBoolExp(string &reg_source) {
    auto reg_bitcast = GenRegister();

    code_buffer.emit(
            reg_bitcast + " = trunc i32 " + reg_source + " to i1");

    auto branch_addr = code_buffer.emit("br i1 " + reg_bitcast + ", label @, label @");


    return make_shared<STypeBoolExp>(CodeBuffer::makelist({branch_addr, FIRST}),
                                     CodeBuffer::makelist({branch_addr, SECOND}));
}

STypeStatementPtr CodeGen::EmitStatementCall() {
    auto statement = make_shared<STypeStatement>(branch_list());
    return statement;
}

STypeStatementPtr CodeGen::EmitStatementReturn() {
    auto statement = make_shared<STypeStatement>(branch_list());
    code_buffer.emit("ret void");
    return statement;
}

STypeStatementPtr CodeGen::EmitStatementReturnExp(const STypePtr &exp) {
    auto statement = make_shared<STypeStatement>(branch_list());
    string reg_result;

    if (exp->general_type == BOOL_TYPE) {
        reg_result = GenRegister();
        EmitBoolExpToRegister(exp, reg_result);

    } else {
        reg_result = GetNonBoolExpString(exp);
    }

    code_buffer.emit("ret " + GetLLVMType(exp->general_type) + " " + reg_result);
    return statement;
}

STypeStatementPtr
CodeGen::EmitStatementIf(const STypePtr &exp, const STypePtr &if_label, const STypePtr &if_statement,
                         const STypePtr &if_list_as_statement) {
    auto dynamic_cast_bool_exp = dynamic_pointer_cast<STypeBoolExp>(exp);
    auto dynamic_cast_if_statement = dynamic_pointer_cast<STypeStatement>(if_statement);
    auto dynamic_cast_if_label = dynamic_pointer_cast<STypeString>(if_label);
    auto dynamic_cast_if_list_as_statement = dynamic_pointer_cast<STypeStatement>(if_list_as_statement);


    // bpatch true
    code_buffer.bpatch(dynamic_cast_bool_exp->true_list, dynamic_cast_if_label->token);

    // merge false and next
    auto statement = dynamic_cast_if_statement;
    statement->next_list = CodeBuffer::merge(dynamic_cast_if_list_as_statement->next_list,
                                             CodeBuffer::merge(statement->next_list,
                                                               dynamic_cast_bool_exp->false_list));

    return statement;
}

STypeStatementPtr
CodeGen::EmitStatementIfElse(const STypePtr &exp, const STypePtr &if_label, const STypePtr &if_statement,
                             STypePtr if_list_as_statement,
                             const STypePtr &else_label, const STypePtr &else_statement) {
    auto dynamic_cast_bool_exp = dynamic_pointer_cast<STypeBoolExp>(exp);
    auto dynamic_cast_if_statement = dynamic_pointer_cast<STypeStatement>(if_statement);
    auto dynamic_cast_else_statement = dynamic_pointer_cast<STypeStatement>(else_statement);
    auto dynamic_cast_if_label = dynamic_pointer_cast<STypeString>(if_label);
    auto dynamic_cast_else_label = dynamic_pointer_cast<STypeString>(else_label);
    auto dynamic_cast_if_list_as_statement = dynamic_pointer_cast<STypeStatement>(if_list_as_statement);

    // bpatch true and false
    code_buffer.bpatch(dynamic_cast_bool_exp->true_list, dynamic_cast_if_label->token);
    code_buffer.bpatch(dynamic_cast_bool_exp->false_list, dynamic_cast_else_label->token);

    // merge if's next (includes if_list) and else's next
    auto statement = dynamic_cast_if_statement;
    statement->next_list = CodeBuffer::merge(
            CodeBuffer::merge(statement->next_list, dynamic_cast_else_statement->next_list),
            dynamic_cast_if_list_as_statement->next_list);

    return statement;
}

STypeStatementPtr
CodeGen::EmitStatementWhile(STypePtr start_list_as_statement, const STypePtr &while_head_label, const STypePtr &exp,
                            const STypePtr &while_body_label, const STypePtr &while_statement,
                            const STypePtr &end_list_as_statement, const branch_list_ptr &break_list) {
    auto dynamic_cast_start_list_as_statement = dynamic_pointer_cast<STypeStatement>(start_list_as_statement);
    auto dynamic_cast_bool_exp = dynamic_pointer_cast<STypeBoolExp>(exp);
    auto dynamic_cast_while_statement = dynamic_pointer_cast<STypeStatement>(while_statement);
    auto dynamic_cast_while_head_label = dynamic_pointer_cast<STypeString>(while_head_label);
    auto dynamic_cast_while_body_label = dynamic_pointer_cast<STypeString>(while_body_label);
    auto dynamic_cast_end_list_as_statement = dynamic_pointer_cast<STypeStatement>(end_list_as_statement);

    // bpatch true->body->head, start->head
    code_buffer.bpatch(dynamic_cast_bool_exp->true_list, dynamic_cast_while_body_label->token);
    code_buffer.bpatch(dynamic_cast_while_statement->next_list, dynamic_cast_while_head_label->token);
    code_buffer.bpatch(dynamic_cast_end_list_as_statement->next_list, dynamic_cast_while_head_label->token);
    code_buffer.bpatch(dynamic_cast_start_list_as_statement->next_list, dynamic_cast_while_head_label->token);


    // false & break go to next
    auto statement = make_shared<STypeStatement>(CodeBuffer::merge(dynamic_cast_bool_exp->false_list,
                                                                   *break_list));

    return statement;
}

STypeStatementPtr CodeGen::EmitStatementBreak() {
    // add address to `break_list`
    semantic_ref.table_ref.scope_stack.top()->break_list->push_back({code_buffer.emit("br label @  ; break"), FIRST});

    auto statement = make_shared<STypeStatement>(branch_list());
    return statement;
}

STypeStatementPtr CodeGen::EmitStatementContinue() {
    code_buffer.emit("br label %" + semantic_ref.table_ref.scope_stack.top()->while_continue_label);
    auto statement = make_shared<STypeStatement>(branch_list());
    return statement;
}

STypeStatementPtr
CodeGen::EmitStatementSwitch(STypePtr exp, STypePtr switch_list_as_statement, STypePtr case_list,
                             branch_list_ptr break_list) {
    auto dynamic_cast_exp = GetNonBoolExpString(exp);
    auto dynamic_cast_switch_list = dynamic_pointer_cast<STypeStatement>(switch_list_as_statement);
    auto dynamic_cast_case_list = dynamic_pointer_cast<STypeCaseList>(case_list);


    //  break_list -> next
    auto statement = make_shared<STypeStatement>(*break_list);

    // emit init label and backpatch switch head
    auto init_label = code_buffer.genLabel("_switch_init");
    code_buffer.bpatch(dynamic_cast_switch_list->next_list, init_label);

    // caselist -> next
    statement->next_list = CodeBuffer::merge(statement->next_list, dynamic_cast_case_list->next_list);

    // emit comparisons
    string last_label;
    branch_list curr_branch_pair;
    for (size_t i = 0; i < dynamic_cast_case_list->case_list.size(); ++i) {

        if (i > 0) {
            // backpatch
            last_label = code_buffer.genLabel("_switch_comparison");
            code_buffer.bpatch(curr_branch_pair, last_label);
        }

        // comparison
        auto reg_icmp = GenRegister();
        string icmp_string(reg_icmp + " = icmp eq i32 ");
        icmp_string += dynamic_cast_exp + ", ";
        icmp_string += to_string(dynamic_cast_case_list->case_list[i].first);
        code_buffer.emit(icmp_string);

        // branch - the last branch is not backpatched
        auto branch_addr = code_buffer.emit("br i1 " + reg_icmp + ", label %" +
                                            dynamic_cast_case_list->case_list[i].second + ", label @");
        curr_branch_pair.push_back({branch_addr, SECOND});


    }


    // emit default branch if needed
    if (!dynamic_cast_case_list->default_label.empty()) {
        code_buffer.emit("br label %" + dynamic_cast_case_list->default_label);
        code_buffer.bpatch(curr_branch_pair, dynamic_cast_case_list->default_label);
    }else{
        // backpatch last branch if no default
        last_label = code_buffer.genLabel("_switch_comparison_end");
        code_buffer.bpatch(curr_branch_pair, last_label);
    }



    return statement;
}

void CodeGen::EmitFuncDecl(const STypePtr &statements, const STypePtr &next_label) {
    // ret things
    auto curr_func_ret_type = semantic_ref.table_ref.scope_stack.top()->ret_type;
    if (curr_func_ret_type == VOID_TYPE) {
        code_buffer.emit("ret void");
    } else {
        code_buffer.emit("ret i32 0");
    }

    // func things
    code_buffer.emit("}");
    stack_register.clear();
    semantic_ref.table_ref.PopScope();


    // statements things
    auto dynamic_cast_statements = dynamic_pointer_cast<STypeStatement>(statements);
    auto dynamic_cast_next_label = dynamic_pointer_cast<STypeString>(next_label);

    code_buffer.bpatch(dynamic_cast_statements->next_list, dynamic_cast_next_label->token);
}

STypeBoolExpPtr CodeGen::EmitTrue() {
    return make_shared<STypeBoolExp>(CodeBuffer::makelist({code_buffer.emit("br label @"), FIRST}), branch_list());
}

STypeBoolExpPtr CodeGen::EmitFalse() {
    return make_shared<STypeBoolExp>(branch_list(), CodeBuffer::makelist({code_buffer.emit("br label @"), FIRST}));
}

STypeBoolExpPtr CodeGen::EmitNot(const STypePtr &bool_exp) {
    auto dynamic_cast_bool_exp = dynamic_pointer_cast<STypeBoolExp>(bool_exp);

    branch_list old_true_list(dynamic_cast_bool_exp->true_list);
    dynamic_cast_bool_exp->true_list = dynamic_cast_bool_exp->false_list;
    dynamic_cast_bool_exp->false_list = old_true_list;

    return dynamic_cast_bool_exp;
}

STypeBoolExpPtr CodeGen::EmitAnd(const STypePtr &bool_exp1, const STypePtr &and_label, const STypePtr &bool_exp2) {
    auto dynamic_cast_bool_exp1 = dynamic_pointer_cast<STypeBoolExp>(bool_exp1);
    auto dynamic_cast_and_label = dynamic_pointer_cast<STypeString>(and_label);
    auto dynamic_cast_bool_exp2 = dynamic_pointer_cast<STypeBoolExp>(bool_exp2);

    // exp1: true -> exp2, false -> and_false
    code_buffer.bpatch(dynamic_cast_bool_exp1->true_list, dynamic_cast_and_label->token);

    // exp2: true -> and_true, false -> and_false
    return make_shared<STypeBoolExp>(dynamic_cast_bool_exp2->true_list,
                                     CodeBuffer::merge(dynamic_cast_bool_exp1->false_list,
                                                       dynamic_cast_bool_exp2->false_list));
}

STypeBoolExpPtr CodeGen::EmitOr(const STypePtr &bool_exp1, const STypePtr &or_label, const STypePtr &bool_exp2) {
    auto dynamic_cast_bool_exp1 = dynamic_pointer_cast<STypeBoolExp>(bool_exp1);
    auto dynamic_cast_or_label = dynamic_pointer_cast<STypeString>(or_label);
    auto dynamic_cast_bool_exp2 = dynamic_pointer_cast<STypeBoolExp>(bool_exp2);

    // exp1: true -> or_true, false -> exp2
    code_buffer.bpatch(dynamic_cast_bool_exp1->false_list, dynamic_cast_or_label->token);

    // exp2: true -> or_true, false -> or_false
    return make_shared<STypeBoolExp>(CodeBuffer::merge(dynamic_cast_bool_exp1->true_list,
                                                       dynamic_cast_bool_exp2->true_list),
                                     dynamic_cast_bool_exp2->false_list);
}

STypeRegisterPtr CodeGen::EmitString(const STypePtr &stype_string) {
    auto dynamic_cast_string = dynamic_pointer_cast<STypeString>(stype_string);

    auto reg_result = make_shared<STypeRegister>(GenRegister(), STRING_TYPE);
    auto reg_global = GenGlobalRegister();
    auto string_size_string = to_string(dynamic_cast_string->token.size() + 1);

    code_buffer.emitGlobal(
            reg_global + " = constant [" + string_size_string +
            " x i8] c\"" + dynamic_cast_string->token + "\\00\"");

    code_buffer.emit(reg_result->reg_name + " = getelementptr [" + string_size_string +
                     " x i8], [" + string_size_string + " x i8]* " + reg_global + ", i32 0, i32 0");

    return reg_result;
}

STypePtr CodeGen::EmitID(const STypeSymbolPtr &symbol) {
    auto dynamic_cast_symbol = dynamic_pointer_cast<STypeSymbol>(symbol);

    if (dynamic_cast_symbol->general_type == FUNCTION_TYPE){
        // do not convert function id to register
        return dynamic_cast_symbol;
    }

    auto exp_reg = EmitLoadRegister(dynamic_cast_symbol->offset, dynamic_cast_symbol->general_type);
    if (exp_reg->general_type == BOOL_TYPE) {
        // exp should be a bool_exp when possible
        return RegisterToBoolExp(exp_reg->reg_name);
    }

    return exp_reg;
}

STypeCaseListPtr CodeGen::EmitCaseList(STypePtr case_decl, STypePtr next_label, STypePtr case_list) {
    auto dynamic_cast_case_decl = dynamic_pointer_cast<STypeCaseDecl>(case_decl);
    auto dynamic_cast_next_label = dynamic_pointer_cast<STypeString>(next_label);
    auto dynamic_cast_case_list = dynamic_pointer_cast<STypeCaseList>(case_list);

    // backpatch the statements in the last case declaration
    code_buffer.bpatch(dynamic_cast_case_decl->next_list, dynamic_cast_next_label->token);

    auto case_label_list = dynamic_cast_case_list->case_list;
    case_label_list.emplace_back(dynamic_cast_case_decl->case_num, dynamic_cast_case_decl->case_label);

    auto default_label = dynamic_cast_case_list->default_label;

    auto next_list = dynamic_cast_case_list->next_list;

    auto result_case_list = make_shared<STypeCaseList>(case_label_list, default_label, next_list);

    return result_case_list;
}

STypeCaseListPtr CodeGen::EmitCaseList(STypePtr case_decl) {
    auto dynamic_cast_case_decl = dynamic_pointer_cast<STypeCaseDecl>(case_decl);

    auto case_list = case_label_list();
    case_list.emplace_back(dynamic_cast_case_decl->case_num, dynamic_cast_case_decl->case_label);

    auto result_case_list = make_shared<STypeCaseList>(case_list,
                                                       "",
                                                       dynamic_cast_case_decl->next_list);

    return result_case_list;
}

STypeCaseListPtr CodeGen::EmitCaseDefault(STypePtr list_as_statement, STypePtr default_label, STypePtr statements) {
    auto dynamic_cast_next_list = dynamic_pointer_cast<STypeStatement>(list_as_statement);
    auto dynamic_cast_case_default_label = dynamic_pointer_cast<STypeString>(default_label);
    auto dynamic_cast_statements = dynamic_pointer_cast<STypeStatement>(statements);

    auto result_case_list = make_shared<STypeCaseList>(case_label_list(),
                                                       dynamic_cast_case_default_label->token,
                                                       dynamic_cast_statements->next_list);

    code_buffer.bpatch(dynamic_cast_next_list->next_list, dynamic_cast_case_default_label->token);

    return result_case_list;
}

STypeCaseDeclPtr
CodeGen::EmitCaseDecl(STypePtr num, STypePtr list_as_statement, STypePtr case_decl_label, STypePtr statements) {
    auto dynamic_cast_num = dynamic_pointer_cast<STypeNumber>(num);
    auto dynamic_cast_case_decl_label = dynamic_pointer_cast<STypeString>(case_decl_label);
    auto dynamic_cast_statements = dynamic_pointer_cast<STypeStatement>(statements);
    auto dynamic_cast_next_list = dynamic_pointer_cast<STypeStatement>(list_as_statement);

    auto case_decl_result = make_shared<STypeCaseDecl>(dynamic_cast_num->token,
                                                       dynamic_cast_case_decl_label->token,
                                                       dynamic_cast_statements->next_list);

    code_buffer.bpatch(dynamic_cast_next_list->next_list, dynamic_cast_case_decl_label->token);

    return case_decl_result;

}


STypeStatementPtr CodeGen::EmitBranchNext() {
    // using statement because i'm too lazy to create a new class
    auto fake_statement = make_shared<STypeStatement>(
            CodeBuffer::makelist({code_buffer.emit("br label @  ; end of statement"), FIRST}));
    return fake_statement;
}

STypeStatementPtr CodeGen::EmitBranchWhileHead() {
    auto fake_statement = make_shared<STypeStatement>(
            CodeBuffer::makelist({code_buffer.emit("br label @  ; start/end of while"), FIRST}));
    return fake_statement;
}

STypeStatementPtr CodeGen::EmitParseBranchIfNext() {
    auto fake_statement = make_shared<STypeStatement>(
            CodeBuffer::makelist({code_buffer.emit("br label @  ; end of if"), FIRST}));
    return fake_statement;
}

STypeStatementPtr CodeGen::EmitBranchSwitchHead() {
    auto fake_statement = make_shared<STypeStatement>(
            CodeBuffer::makelist({code_buffer.emit("br label @  ; start of switch"), FIRST}));
    return fake_statement;
}

STypeStatementPtr CodeGen::EmitBranchCaseHead() {
    auto fake_statement = make_shared<STypeStatement>(
            CodeBuffer::makelist({code_buffer.emit("br label @  ; start of case"), FIRST}));
    return fake_statement;
}

STypeStatementPtr CodeGen::EmitBranchDefaultHead() {
    auto fake_statement = make_shared<STypeStatement>(
            CodeBuffer::makelist({code_buffer.emit("br label @  ; start of default"), FIRST}));
    return fake_statement;
}

STypePtr CodeGen::EmitCallExp(STypePtr call_exp) {
    auto dynamic_cast_call_exp = dynamic_pointer_cast<STypeRegister>(call_exp);
    if (call_exp->general_type == BOOL_TYPE) {
        return RegisterToBoolExp(dynamic_cast_call_exp->reg_name);
    } else {
        return call_exp;
    }

}

