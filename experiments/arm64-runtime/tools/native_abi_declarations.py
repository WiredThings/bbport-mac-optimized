"""Recognize the ABI marker in Clang AST, regardless of macro spelling/location."""
GUEST_ABI_TAG=0x42424755455354

def is_guest_definition(node):
    if node.get('kind')!='FunctionDecl':return False
    children=node.get('inner',[])
    if not any(x.get('kind')=='CompoundStmt'for x in children):return False
    return any(attr.get('kind')=='AnnotateAttr'and any(
        value.get('kind')=='ConstantExpr'and value.get('value')==str(GUEST_ABI_TAG)
        for value in attr.get('inner',[]))for attr in children)
